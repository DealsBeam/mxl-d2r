"""Publish a tools/package_release.py output to the download host (docs/release.md "Hosting layout", docs/hosting.md).

The host is a plain static HTTPS server (Caddy file_server on dl.werdegames.com/mxl/); nothing runs there. Layout under the
target root:

  channels/<channel>.json                   {"schemaVersion":1,"version","manifest","manifestSha256","publishedAt"}
  releases/<v>/release-manifest.json        the packager's manifest + "objectsBase":"objects/" (+ zip.path, launcher)
  releases/<v>/MedianXL-D2R-<v>.zip, SHA256SUMS, RELEASE_NOTES-<v>.md    manual / offline fallback
  objects/<sha256[0:2]>/<sha256>            raw file bytes, content-addressed, shared by every version
  launcher/<v>/MedianXL-Launcher.exe, .sha256    (--launcher)
  assets/<files>                            the asset pack (--assets)

Order of a publish: missing objects, zip + sums, launcher, assets, the release manifest, and the channel file LAST, so a
half-finished publish is never visible to players. Every file is written under a temp name and renamed. Re-running is safe:
what is there (right size) is not uploaded again; a version whose manifest differs from the published one is refused.

Targets:
  --target dir:<path>                       a local folder (tests; or a run on the server itself)
  --target sftp://user@host[:port]/<path>   OpenSSH `sftp` in batch mode (key auth; `--identity KEY`). <path> is as the SFTP
                                            server sees it (inside a chroot: /mxl); `/~/x` means x under the login home.
  (the target can also come from the environment variable MXL_PUBLISH_TARGET)

  py -3 tools/publish_release.py publish --version 0.70.0 --channel test --target dir:D:\\site
  py -3 tools/publish_release.py promote 0.70.0 --channel stable --target sftp://upload@host/mxl
  py -3 tools/publish_release.py list --target ...
  py -3 tools/publish_release.py gc --keep 3 [--dry-run] --target ...
  py -3 tools/publish_release.py verify --url https://dl.werdegames.com/mxl/ [--channel stable] [--sample 200]
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import secrets
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCHEMA = 1
CHANNELS = ("stable", "test")
OBJECTS_BASE = "objects/"
LAUNCHER_NAME = "MedianXL-Launcher.exe"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
PART = ".part-"  # temp-name marker: <final name>.part-<random>
BATCH_FILES = 200
BATCH_BYTES = 256 << 20


def sha256_file(path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def object_path(sha: str) -> str:
    return f"{OBJECTS_BASE}{sha[:2]}/{sha}"


def version_key(v: str):
    """Sort key: 0.70.0 < 0.70.1 < 0.71.0-rc1 < 0.71.0 (a pre-release sorts before its release)."""
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:[-+](.*))?$", v)
    nums = tuple(int(x) for x in m.groups()[:3])
    return nums + (0, m.group(4)) if m.group(4) else nums + (1, "")


def dumps(obj) -> bytes:
    return (json.dumps(obj, indent=1) + "\n").encode("utf-8")


def now_utc() -> str:
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def check_version(v: str) -> str:
    v = v.lstrip("v")
    if not VERSION_RE.match(v):
        raise SystemExit(f"bad version {v!r} (expected 1.2.3 or 1.2.3-rc1)")
    return v


class PublishError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------- targets
class Target:
    """The host's folder. Paths are '/'-separated and relative to the target root. Files appear only complete (temp name,
    then rename); `put` overwrites. list_dir -> {name: (size, is_dir)}, or None when the folder does not exist."""

    def list_dir(self, rel):  # pragma: no cover - interface
        raise NotImplementedError

    def list_dirs(self, rels):
        return {r: self.list_dir(r) for r in rels}

    def read(self, rel):
        raise NotImplementedError

    def put_files(self, items):
        """items: [(local Path, rel)]"""
        raise NotImplementedError

    def put_bytes(self, data: bytes, rel):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "payload"
            p.write_bytes(data)
            self.put_files([(p, rel)])

    def delete_file(self, rel):
        raise NotImplementedError

    def delete_files(self, rels):
        for rel in rels:
            self.delete_file(rel)

    def delete_dir(self, rel):
        """Remove a folder and everything in it."""
        listing = self.list_dir(rel)
        if listing is None:
            return
        for name, (_, is_dir) in sorted(listing.items()):
            (self.delete_dir if is_dir else self.delete_file)(f"{rel}/{name}")
        self._rmdir(rel)

    def _rmdir(self, rel):
        raise NotImplementedError


def tmp_name(rel: str) -> str:
    return f"{rel}{PART}{secrets.token_hex(4)}"


class DirTarget(Target):
    def __init__(self, root):
        self.root = Path(root)

    def _p(self, rel):
        return self.root.joinpath(*rel.split("/"))

    def list_dir(self, rel):
        p = self._p(rel)
        if not p.is_dir():
            return None
        return {c.name: (c.stat().st_size if c.is_file() else 0, c.is_dir()) for c in p.iterdir()}

    def read(self, rel):
        p = self._p(rel)
        return p.read_bytes() if p.is_file() else None

    def put_files(self, items):
        for local, rel in items:
            dest = self._p(rel)
            dest.parent.mkdir(parents=True, exist_ok=True)
            tmp = self._p(tmp_name(rel))
            shutil.copyfile(local, tmp)
            os.replace(tmp, dest)

    def delete_file(self, rel):
        self._p(rel).unlink(missing_ok=True)

    def _rmdir(self, rel):
        try:
            self._p(rel).rmdir()
        except OSError:
            pass

    def __str__(self):
        return f"dir:{self.root}"


LS_RE = re.compile(r"^([-dlcbps\w])[-rwxsStTl+.@]{8,10}\s+\S+\s+\S+\s+\S+\s+(\d+)\s+\S+\s+\d+\s+\S+\s+(.+?)\s*$")
ECHO = "sftp> "


def sftp_quote(s) -> str:
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse_sftp_ls(lines):
    """`ls -ln` output lines -> {name: (size, is_dir)}."""
    out = {}
    for ln in lines:
        m = LS_RE.match(ln)
        if not m or m.group(3) in (".", ".."):
            continue
        out[m.group(3).rsplit("/", 1)[-1]] = (int(m.group(2)), m.group(1) == "d")
    return out


class SftpTarget(Target):
    """OpenSSH sftp in batch mode, no shell on the server needed (works for an internal-sftp chroot user). Each call is one
    ssh login that runs a batch of commands, so many files go through one connection."""

    def __init__(self, user, host, port, path, identity=None, sftp_cmd=None):
        self.user, self.host, self.port, self.path = user, host, port, path
        self.identity = identity
        self.cmd = sftp_cmd or shlex.split(os.environ.get("MXL_SFTP", "sftp"))

    @classmethod
    def parse(cls, url, identity=None, sftp_cmd=None):
        m = re.match(r"^sftp://(?:([^@/]+)@)?([^:/]+)(?::(\d+))?(/.*)?$", url)
        if not m:
            raise SystemExit(f"bad sftp target {url!r}; expected sftp://user@host[:port]/path")
        path = m.group(4) or ""
        if path.startswith("/~/"):
            path = path[3:]
        elif path == "/~":
            path = ""
        return cls(m.group(1), m.group(2), int(m.group(3)) if m.group(3) else None, path.rstrip("/"), identity, sftp_cmd)

    def _argv(self):
        a = list(self.cmd) + ["-b", "-", "-oBatchMode=yes", "-oConnectTimeout=20", "-oServerAliveInterval=30"]
        if self.port:
            a += ["-P", str(self.port)]
        if self.identity:
            a += ["-i", str(self.identity), "-oIdentitiesOnly=yes"]
        return a + [f"{self.user}@{self.host}" if self.user else self.host]

    def _abs(self, rel):
        return f"{self.path}/{rel}" if self.path else rel

    def _run(self, commands):
        """Run commands in one session -> (stdout lines, stderr). Raises PublishError when sftp exits non-zero (a command
        without a leading '-' failed, or the login did)."""
        proc = subprocess.run(self._argv(), input="\n".join(commands) + "\n", capture_output=True, text=True, encoding="utf-8",
                              errors="replace")
        if proc.returncode != 0:
            raise PublishError(f"sftp {self.host} failed ({proc.returncode}): {proc.stderr.strip()[-600:]}")
        return proc.stdout.splitlines(), proc.stderr

    def list_dirs(self, rels):
        """One session for all folders. A folder that does not exist -> None; any other `ls` failure raises."""
        rels = list(rels)
        if not rels:
            return {}
        lines, err = self._run([f"-ls -ln {sftp_quote(self._abs(r))}" for r in rels])
        missing = set(re.findall(r"Can't ls: \"(.*)\" not found", err))
        bad = [ln for ln in err.splitlines() if re.search(r"Couldn't|Can't ls|Permission denied|Failure", ln)
               and not re.search(r"Can't ls: \".*\" not found", ln)]
        if bad:
            raise PublishError("sftp ls failed: " + "; ".join(bad[:3]))
        out, idx = {r: [] for r in rels}, -1
        for ln in lines:
            if ln.startswith(ECHO):  # batch mode echoes each command before its output
                idx += 1
            elif 0 <= idx < len(rels):
                out[rels[idx]].append(ln)
        return {r: (None if self._abs(r) in missing else parse_sftp_ls(out[r])) for r in rels}

    def list_dir(self, rel):
        return self.list_dirs([rel])[rel]

    def read(self, rel):
        parent, _, name = rel.rpartition("/")
        listing = self.list_dir(parent)
        if not listing or name not in listing:
            return None
        with tempfile.TemporaryDirectory() as d:
            dest = Path(d) / "got"
            self._run([f"get {sftp_quote(self._abs(rel))} {sftp_quote(dest.as_posix())}"])
            return dest.read_bytes()

    def _mkdirs(self, rels):
        dirs = set()
        for rel in rels:
            parts = rel.split("/")[:-1]
            for i in range(1, len(parts) + 1):
                dirs.add("/".join(parts[:i]))
        return [f"-mkdir {sftp_quote(self._abs(d))}" for d in sorted(dirs, key=lambda d: (d.count("/"), d))]

    def put_files(self, items):
        items = list(items)
        for i in range(0, len(items), BATCH_FILES):
            chunk = items[i:i + BATCH_FILES]
            cmds = self._mkdirs([rel for _, rel in chunk])
            tmps = []
            for local, rel in chunk:
                t = tmp_name(rel)
                tmps.append((t, rel))
                cmds.append(f"put {sftp_quote(Path(local).as_posix())} {sftp_quote(self._abs(t))}")
            # OpenSSH's sftp-server replaces an existing target atomically (the posix-rename extension). A server without it
            # refuses; then the temp file is still there, and the old file is removed just before the rename.
            cmds += [f"-rename {sftp_quote(self._abs(t))} {sftp_quote(self._abs(rel))}" for t, rel in tmps]
            self._run(cmds)
            parents = sorted({rel.rpartition("/")[0] for _, rel in chunk})
            left = self.list_dirs(parents)
            stuck = [(t, rel) for t, rel in tmps if t.rpartition("/")[2] in (left[rel.rpartition("/")[0]] or {})]
            if stuck:
                self._run([c for t, rel in stuck for c in (f"-rm {sftp_quote(self._abs(rel))}",
                                                           f"rename {sftp_quote(self._abs(t))} {sftp_quote(self._abs(rel))}")])

    def delete_file(self, rel):
        self._run([f"rm {sftp_quote(self._abs(rel))}"])

    def delete_files(self, rels):
        rels = list(rels)
        for i in range(0, len(rels), BATCH_FILES * 5):  # one login per 1000 files; a file that is already gone is fine
            self._run([f"-rm {sftp_quote(self._abs(r))}" for r in rels[i:i + BATCH_FILES * 5]])

    def _rmdir(self, rel):
        self._run([f"-rmdir {sftp_quote(self._abs(rel))}"])

    def __str__(self):
        return f"sftp://{self.user + '@' if self.user else ''}{self.host}{':' + str(self.port) if self.port else ''}/{self.path.lstrip('/')}"


def open_target(spec, identity=None) -> Target:
    spec = spec or os.environ.get("MXL_PUBLISH_TARGET")
    if not spec:
        raise SystemExit("no target: --target dir:<path> | sftp://user@host[:port]/path (or MXL_PUBLISH_TARGET)")
    if spec.startswith("dir:"):
        return DirTarget(spec[4:])
    if spec.startswith("sftp://"):
        return SftpTarget.parse(spec, identity)
    raise SystemExit(f"unknown target {spec!r}")


# --------------------------------------------------------------------------------------------------------------- sources
class ZipSource:
    """File bytes of a release from the packager's zip (the staging folder is gone after packaging). Every file is hashed
    while it is extracted."""

    def __init__(self, zip_path, top="MedianXL"):
        self.z = zipfile.ZipFile(zip_path)
        self.top = top

    def extract(self, path, dest: Path) -> str:
        h = hashlib.sha256()
        dest.parent.mkdir(parents=True, exist_ok=True)
        with self.z.open(f"{self.top}/{path}") as src, open(dest, "wb") as out:
            for chunk in iter(lambda: src.read(1 << 20), b""):
                h.update(chunk)
                out.write(chunk)
        return h.hexdigest()

    def close(self):
        self.z.close()


class DirSource:
    """File bytes from a staged mod folder (the MedianXL/ folder itself)."""

    def __init__(self, folder):
        self.folder = Path(folder)

    def extract(self, path, dest: Path) -> str:
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.folder / path, dest)
        return sha256_file(dest)

    def close(self):
        pass


# ------------------------------------------------------------------------------------------------------------- publishing
def load_manifest(dist: Path, version: str, allow_incomplete: bool, channel):
    man_path = dist / "release-manifest.json"
    if not man_path.is_file():
        raise PublishError(f"{man_path} not found: run tools/package_release.py first")
    man = json.loads(man_path.read_text("utf-8"))
    if man.get("schemaVersion") != 1:
        raise PublishError(f"unknown manifest schemaVersion {man.get('schemaVersion')}")
    if man.get("version") != version:
        raise PublishError(f"{man_path} is version {man.get('version')}, not {version}")
    if not man.get("complete", False):
        if channel == "stable" or not allow_incomplete:
            raise PublishError("the manifest says complete=false (a --no-data CI package): not for players "
                               "(--allow-incomplete publishes it to the test channel or with --no-channel)")
    for f in man["files"]:
        if not SHA_RE.match(f["sha256"]) or ".." in f["path"].split("/") or f["path"].startswith("/"):
            raise PublishError(f"bad manifest entry {f['path']!r}")
    return man


def object_table(files):
    """sha -> (size, first path): files with the same bytes (across versions, and inside one) are one object."""
    objs = {}
    for f in files:
        prev = objs.setdefault(f["sha256"], (f["size"], f["path"]))
        if prev[0] != f["size"]:
            raise PublishError(f"{f['path']} and {prev[1]} have the same sha256 but different sizes")
    return objs


def list_objects(target: Target, shas):
    """-> {sha: size} of the listed objects present on the target. Lists only the prefix folders in use."""
    prefixes = sorted({s[:2] for s in shas})
    listings = target.list_dirs([f"objects/{p}" for p in prefixes])
    have = {}
    for p in prefixes:
        for name, (size, is_dir) in (listings[f"objects/{p}"] or {}).items():
            if SHA_RE.match(name) and not is_dir:
                have[name] = size
    return have


def missing_objects(target: Target, objs):
    """Objects the target lacks or has with the wrong size: {sha: (size, path)}."""
    have = list_objects(target, objs)
    return {s: v for s, v in objs.items() if have.get(s) != v[0]}


def upload_objects(target: Target, source, missing, log=print):
    """Extract the missing objects in batches and upload them (temp name + rename). Returns bytes sent."""
    order = sorted(missing.items(), key=lambda kv: kv[1][1])
    total = sum(v[0] for _, v in order)
    done = sent = 0
    i = 0
    while i < len(order):
        batch, size = [], 0
        while i < len(order) and len(batch) < BATCH_FILES and (not batch or size + order[i][1][0] <= BATCH_BYTES):
            batch.append(order[i])
            size += order[i][1][0]
            i += 1
        with tempfile.TemporaryDirectory(prefix="mxl-publish-") as tmp:
            items = []
            for sha, (_, path) in batch:
                local = Path(tmp) / sha
                got = source.extract(path, local)
                if got != sha:
                    raise PublishError(f"{path}: sha256 {got} does not match the manifest ({sha}); package again")
                items.append((local, object_path(sha)))
            target.put_files(items)
        done += len(batch)
        sent += size
        log(f"  objects {done}/{len(order)}  {sent / 1e6:.0f}/{total / 1e6:.0f} MB")
    return sent


def put_if_needed(target: Target, local: Path, rel: str, force=False, log=print) -> bool:
    """Upload a plain file unless the target has one of the same name and size (zip, launcher, asset-pack files)."""
    parent, _, name = rel.rpartition("/")
    cur = (target.list_dir(parent) or {}).get(name)
    if cur and not cur[1] and cur[0] == local.stat().st_size and not force:
        log(f"  {rel}: already there")
        return False
    target.put_files([(local, rel)])
    log(f"  {rel}: uploaded ({local.stat().st_size / 1e6:.1f} MB)")
    return True


def sums_text(entries):
    return "".join(f"{sha}  {name}\n" for name, sha in entries)


def publish(target: Target, dist: Path, version: str, channel, source=None, launcher=None, launcher_version=None,
            assets=None, allow_incomplete=False, force=False, verify=True, log=print):
    version = check_version(version)
    man = load_manifest(dist, version, allow_incomplete, channel)
    zip_path = dist / man["zip"]["name"]
    if not zip_path.is_file():
        raise PublishError(f"{zip_path} not found")
    log(f"checking {zip_path.name} against the manifest")
    if zip_path.stat().st_size != man["zip"]["size"] or sha256_file(zip_path) != man["zip"]["sha256"]:
        raise PublishError(f"{zip_path.name} does not match zip.size / zip.sha256 of the manifest")
    own_source = source is None
    source = source or ZipSource(zip_path, man.get("modFolder", "MedianXL"))
    try:
        # the published manifest: the packager's, plus the host's fields
        pub = dict(man)
        pub["objectsBase"] = OBJECTS_BASE
        pub["zip"] = dict(man["zip"], path=f"releases/{version}/{man['zip']['name']}")
        if launcher:
            lv = check_version(launcher_version or version)
            pub["launcher"] = {"version": lv, "path": f"launcher/{lv}/{LAUNCHER_NAME}", "size": Path(launcher).stat().st_size,
                               "sha256": sha256_file(launcher)}
        man_bytes = dumps(pub)
        man_rel = f"releases/{version}/release-manifest.json"

        old = target.read(man_rel)
        if old is not None and old != man_bytes and not force:
            strip = lambda d: {k: v for k, v in d.items() if k != "released"}  # noqa: E731
            if strip(json.loads(old)) != strip(pub):
                raise PublishError(f"{man_rel} is already published with different contents: a published version never "
                                   f"changes; build a new version (or --force)")
            man_bytes = old  # same content, only the packaging date differs: keep the published bytes
            log(f"{man_rel}: already published, kept")

        objs = object_table(man["files"])
        log(f"{len(man['files'])} files = {len(objs)} distinct objects")
        miss = missing_objects(target, objs)
        log(f"{len(objs) - len(miss)} objects already on the host, {len(miss)} to upload "
            f"({sum(v[0] for v in miss.values()) / 1e6:.0f} MB)")
        upload_objects(target, source, miss, log)

        put_if_needed(target, zip_path, f"releases/{version}/{zip_path.name}", force, log)
        notes = dist / f"RELEASE_NOTES-{version}.md"
        sums = [(zip_path.name, man["zip"]["sha256"]), ("release-manifest.json", sha256_bytes(man_bytes))]
        if notes.is_file():
            target.put_files([(notes, f"releases/{version}/{notes.name}")])
            sums.append((notes.name, sha256_file(notes)))
        target.put_bytes(sums_text(sums).encode(), f"releases/{version}/SHA256SUMS")
        if launcher:
            lp = f"launcher/{pub['launcher']['version']}"
            put_if_needed(target, Path(launcher), f"{lp}/{LAUNCHER_NAME}", force, log)
            target.put_bytes(f"{pub['launcher']['sha256']}  {LAUNCHER_NAME}\n".encode(), f"{lp}/{LAUNCHER_NAME}.sha256")
        if assets:
            for p in sorted(Path(assets).iterdir()):
                if p.is_file():
                    put_if_needed(target, p, f"assets/{p.name}", force, log)
        if verify:
            still = missing_objects(target, objs)
            if still:
                raise PublishError(f"{len(still)} objects are still missing after the upload, e.g. {next(iter(still))}")
        target.put_bytes(man_bytes, man_rel)
        man_sha = sha256_bytes(man_bytes)
        if channel:
            write_channel(target, channel, version, man_sha, log)  # LAST
        return man_sha
    finally:
        if own_source:
            source.close()


def write_channel(target: Target, channel: str, version: str, man_sha: str, log=print):
    body = {"schemaVersion": SCHEMA, "version": version, "manifest": f"releases/{version}/release-manifest.json",
            "manifestSha256": man_sha, "publishedAt": now_utc()}
    target.put_bytes(dumps(body), f"channels/{channel}.json")
    log(f"channel {channel} -> {version}")


def read_json(target: Target, rel):
    raw = target.read(rel)
    return None if raw is None else (json.loads(raw), raw)


def promote(target: Target, version: str, channel: str, log=print):
    version = check_version(version)
    got = read_json(target, f"releases/{version}/release-manifest.json")
    if got is None:
        raise PublishError(f"release {version} is not on the host (publish it first)")
    man, raw = got
    if channel == "stable" and not man.get("complete", False):
        raise PublishError(f"{version} is complete=false: not for the stable channel")
    objs = object_table(man["files"])
    miss = missing_objects(target, objs)
    if miss:
        raise PublishError(f"{version} is missing {len(miss)} objects on the host; publish it again before promoting")
    write_channel(target, channel, version, sha256_bytes(raw), log)


def release_versions(target: Target):
    listing = target.list_dir("releases") or {}
    return sorted((n for n, (_, d) in listing.items() if d and VERSION_RE.match(n)), key=version_key)


def channel_versions(target: Target):
    """Every channels/*.json (not only stable and test: gc must see all of them) -> {channel: parsed file}."""
    out = {}
    for name, (_, is_dir) in sorted((target.list_dir("channels") or {}).items()):
        if name.endswith(".json") and not is_dir:
            out[name[:-5]] = json.loads(target.read(f"channels/{name}"))
    return out


def list_cmd(target: Target, log=print):
    chans = channel_versions(target)
    versions = release_versions(target)
    log(f"target {target}")
    for ch in CHANNELS:
        c = chans.get(ch)
        log(f"channel {ch:7s} " + (f"{c['version']}  published {c['publishedAt']}" if c else "(none)"))
    pointed = {c["version"]: [k for k, v in chans.items() if v["version"] == c["version"]] for c in chans.values()}
    for v in versions:
        got = read_json(target, f"releases/{v}/release-manifest.json")
        if not got:
            log(f"release {v:12s} (no manifest: unfinished)")
            continue
        m = got[0]
        flags = ([f"<- {','.join(pointed[v])}"] if v in pointed else []) + ([] if m.get("complete") else ["incomplete"]) \
            + (["launcher " + m["launcher"]["version"]] if m.get("launcher") else [])
        log(f"release {v:12s} {m['fileCount']} files  {m['totalSize'] / 1e6:.0f} MB  zip {m['zip']['size'] / 1e6:.0f} MB  "
            f"{m.get('released', '')}  {' '.join(flags)}")
    return versions, chans


def gc(target: Target, keep: int, dry_run=False, log=print):
    """Delete releases older than the newest `keep` (never one a channel points to), then the objects no remaining release
    references. Refuses when it cannot read a manifest it needs (a missing reference list would delete live files)."""
    if keep < 1:
        raise PublishError("--keep must be at least 1")
    versions = release_versions(target)
    chans = channel_versions(target)
    pinned = {c["version"] for c in chans.values()}
    manifests = {v: read_json(target, f"releases/{v}/release-manifest.json") for v in versions}
    for v in pinned:
        if manifests.get(v) is None:
            raise PublishError(f"a channel points to {v}, which has no manifest on the host: not collecting anything")
    finished = [v for v in versions if manifests[v] is not None]  # a folder without manifest is an unfinished publish
    keepers = set(finished[-keep:]) | pinned
    doomed = [v for v in versions if v not in keepers]
    refs, launchers = set(), set()
    for v in keepers:
        man = manifests[v][0]
        refs |= {f["sha256"] for f in man["files"]}
        if man.get("launcher"):
            launchers.add(man["launcher"]["version"])
    log(f"keep {', '.join(sorted(keepers, key=version_key))}; remove {', '.join(doomed) or 'no release'}")
    doomed_launchers = sorted({manifests[v][0]["launcher"]["version"] for v in doomed
                               if manifests[v] and manifests[v][0].get("launcher")} - launchers)
    prefixes = [f"objects/{p:02x}" for p in range(256)]
    listings = target.list_dirs(prefixes)
    orphans, stale = [], []
    for pre, listing in listings.items():
        for name, (size, is_dir) in (listing or {}).items():
            if PART in name:
                stale.append((f"{pre}/{name}", size))
            elif SHA_RE.match(name) and name not in refs:
                orphans.append((f"{pre}/{name}", size))
    log(f"{len(orphans)} unreferenced objects ({sum(s for _, s in orphans) / 1e6:.0f} MB), {len(stale)} leftover temp files")
    if dry_run:
        log("dry run: nothing deleted")
        return doomed, orphans
    for v in doomed:  # manifests first: a half-finished gc leaves orphan objects, never a release with holes
        target.delete_file(f"releases/{v}/release-manifest.json")
    for v in doomed:
        target.delete_dir(f"releases/{v}")
    for lv in doomed_launchers:
        target.delete_dir(f"launcher/{lv}")
    target.delete_files(rel for rel, _ in orphans + stale)
    log("done")
    return doomed, orphans


# ------------------------------------------------------------------------------------------------------------------ verify
def http_get(url, headers=None, timeout=60):
    req = urllib.request.Request(url, headers=headers or {})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, dict(r.headers), r.read()


def verify_url(base, channel="stable", sample=0, log=print):
    """Fetch channel -> manifest -> objects over HTTP and check every hash (and Range support on one object). Returns the
    number of objects checked. This is what a client does; the owner runs it against the public URL after setup."""
    base = base.rstrip("/") + "/"
    _, h, raw = http_get(f"{base}channels/{channel}.json")
    ch = json.loads(raw)
    log(f"channel {channel}: {ch['version']} (Cache-Control: {h.get('Cache-Control', '-')})")
    if ch.get("schemaVersion") != SCHEMA:
        raise PublishError(f"channel schemaVersion {ch.get('schemaVersion')}")
    _, _, mraw = http_get(base + ch["manifest"])
    if sha256_bytes(mraw) != ch["manifestSha256"]:
        raise PublishError("manifest sha256 does not match the channel file")
    man = json.loads(mraw)
    objs = object_table(man["files"])
    shas = sorted(objs)
    if sample and sample < len(shas):
        step = len(shas) / sample
        shas = [shas[int(i * step)] for i in range(sample)]
    first = True
    for sha in shas:
        url = base + man.get("objectsBase", OBJECTS_BASE) + f"{sha[:2]}/{sha}"
        _, h, body = http_get(url)
        if sha256_bytes(body) != sha or len(body) != objs[sha][0]:
            raise PublishError(f"{url}: bad content")
        if first:
            log(f"object Cache-Control: {h.get('Cache-Control', '-')}, Content-Encoding: {h.get('Content-Encoding', '-')}")
            first = False
    big = max(shas, key=lambda s: objs[s][0]) if shas else None
    if big and objs[big][0] > 2:
        n = objs[big][0]
        url = base + man.get("objectsBase", OBJECTS_BASE) + f"{big[:2]}/{big}"
        st, h, body = http_get(url, {"Range": "bytes=1-2", "Accept-Encoding": "identity"})
        with urllib.request.urlopen(urllib.request.Request(url, headers={"Range": f"bytes={n - 1}-"})) as r:
            tail = r.read()
        if st != 206 or len(body) != 2 or len(tail) != 1:
            raise PublishError(f"{url}: Range requests are not served (status {st})")
        log("Range requests: ok (206)")
    log(f"{len(shas)} of {len(objs)} objects verified")
    return len(shas)


# --------------------------------------------------------------------------------------------------------------------- cli
def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p, need_target=True):
        p.add_argument("--target", help="dir:<path> | sftp://user@host[:port]/path (default: $MXL_PUBLISH_TARGET)")
        p.add_argument("--identity", help="ssh private key for sftp targets")

    p = sub.add_parser("publish", help="upload a package_release output")
    common(p)
    p.add_argument("--version", required=True)
    p.add_argument("--channel", choices=CHANNELS, default="test", help="channel moved to this version, last (default: test)")
    p.add_argument("--no-channel", action="store_true", help="upload only; promote later")
    p.add_argument("--dist", type=Path, default=ROOT / "dist", help="package_release.py output folder (default: dist)")
    p.add_argument("--stage", type=Path, help="read file bytes from this MedianXL/ folder instead of the zip")
    p.add_argument("--launcher", type=Path, help="MedianXL-Launcher.exe to publish with this release")
    p.add_argument("--launcher-version", help="default: the release version")
    p.add_argument("--assets", type=Path, help="folder of asset-pack files for assets/")
    p.add_argument("--allow-incomplete", action="store_true", help="accept a --no-data package (test channel / --no-channel only)")
    p.add_argument("--force", action="store_true", help="re-upload same-size files; replace a published manifest")
    p.add_argument("--no-verify", action="store_true")

    p = sub.add_parser("promote", help="point a channel at a published version")
    common(p)
    p.add_argument("version")
    p.add_argument("--channel", choices=CHANNELS, required=True)

    p = sub.add_parser("list", help="channels and releases on the host")
    common(p)

    p = sub.add_parser("gc", help="remove old releases and the objects only they used")
    common(p)
    p.add_argument("--keep", type=int, required=True, help="newest N versions to keep (channel versions are always kept)")
    p.add_argument("--dry-run", action="store_true")

    p = sub.add_parser("verify", help="fetch channel, manifest and objects over HTTP(S) and check the hashes")
    p.add_argument("--url", required=True, help="site root, e.g. https://dl.werdegames.com/mxl/")
    p.add_argument("--channel", choices=CHANNELS, default="stable")
    p.add_argument("--sample", type=int, default=0, help="check only N objects spread over the set (default: all)")

    a = ap.parse_args(argv)
    try:
        if a.cmd == "verify":
            verify_url(a.url, a.channel, a.sample)
            return 0
        target = open_target(a.target, a.identity)
        if a.cmd == "publish":
            source = DirSource(a.stage) if a.stage else None
            channel = None if a.no_channel else a.channel
            publish(target, a.dist, a.version, channel, source, a.launcher, a.launcher_version, a.assets,
                    a.allow_incomplete, a.force, not a.no_verify)
        elif a.cmd == "promote":
            promote(target, a.version, a.channel)
        elif a.cmd == "list":
            list_cmd(target)
        elif a.cmd == "gc":
            gc(target, a.keep, a.dry_run)
    except (PublishError, urllib.error.URLError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
