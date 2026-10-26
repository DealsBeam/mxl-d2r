"""tools/publish_release.py and tools/serve_dir.py: dedup, channel last, idempotence, gc safety, the sftp backend (through
tools/fake_sftp.py) and an end-to-end publish -> HTTP -> sha256 check. No game files, no network, no ssh."""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import urllib.request
from pathlib import Path
from unittest import mock

import package_release as pr
import publish_release as pub
import serve_dir

HERE = Path(__file__).resolve().parent
FAKE_SFTP = [sys.executable, str(HERE / "fake_sftp.py")]


def tree(root: Path) -> dict:
    return {p.relative_to(root).as_posix(): p.read_bytes() for p in sorted(root.rglob("*")) if p.is_file()}


class Recorder(pub.DirTarget):
    """A DirTarget that remembers every write (rel paths, in order) and can die at the n-th one."""

    def __init__(self, root, fail_at=0):
        super().__init__(root)
        self.puts, self.fail_at = [], fail_at

    def put_files(self, items):
        for local, rel in items:
            if self.fail_at and len(self.puts) + 1 == self.fail_at:
                raise pub.PublishError("connection lost (test)")
            self.puts.append(rel)
            super().put_files([(local, rel)])

    def objects_put(self):
        return [r for r in self.puts if r.startswith("objects/")]


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: shutil.rmtree(self.tmp, ignore_errors=True))
        self.dll = self.tmp / "mxl-core.dll"
        self.dll.write_bytes(b"MZ..." + pr.plugin_version().encode() + b"\0...")
        self.site = self.tmp / "site"
        self.site.mkdir()

    def make_dist(self, version, files=None, complete=True):
        """A real package_release output (stage, zip, manifest, notes) from a tiny fake data folder."""
        data = self.tmp / f"data-{version}"
        shutil.rmtree(data, ignore_errors=True)
        base = {"global/excel/armor.txt": b"Median armor\n", "global/excel/weapons.txt": b"Median weapons\n",
                "global/excel/copy-of-weapons.txt": b"Median weapons\n"}  # same bytes twice: one object
        for rel, body in (files or base).items():
            (data / rel).parent.mkdir(parents=True, exist_ok=True)
            (data / rel).write_bytes(body)
        out = self.tmp / f"dist-{version}"
        mod = out / "stage" / "MedianXL"
        pr.stage(mod, version, self.dll, self.tmp, data_from=data)
        zip_path = out / f"MedianXL-D2R-{version}.zip"
        pr.write_zip(mod, zip_path)
        man = pr.manifest(version, pr.file_list(mod), zip_path, complete)
        (out / "release-manifest.json").write_text(json.dumps(man, indent=1) + "\n", encoding="utf-8")
        (out / f"RELEASE_NOTES-{version}.md").write_text(f"notes {version}\n", encoding="utf-8")
        shutil.rmtree(out / "stage")
        return out

    def publish(self, version, channel="test", target=None, files=None, **kw):
        target = target or pub.DirTarget(self.site)
        return pub.publish(target, self.make_dist(version, files), version, channel, log=lambda *_: None, **kw)

    def channel(self, name):
        return json.loads((self.site / "channels" / f"{name}.json").read_text("utf-8"))


class PublishTests(Base):
    def test_layout(self):
        man_sha = self.publish("1.0.0", "stable")
        t = tree(self.site)
        self.assertEqual(json.loads(t["channels/stable.json"])["version"], "1.0.0")
        ch = self.channel("stable")
        self.assertEqual(set(ch), {"schemaVersion", "version", "manifest", "manifestSha256", "publishedAt"})
        self.assertEqual(ch["schemaVersion"], 1)
        self.assertEqual(ch["manifest"], "releases/1.0.0/release-manifest.json")
        self.assertEqual(ch["manifestSha256"], man_sha)
        self.assertEqual(pub.sha256_bytes(t[ch["manifest"]]), man_sha)
        man = json.loads(t[ch["manifest"]])
        self.assertEqual(man["objectsBase"], "objects/")
        self.assertEqual(man["schemaVersion"], 1)
        self.assertEqual(man["zip"]["path"], "releases/1.0.0/MedianXL-D2R-1.0.0.zip")
        for f in man["files"]:
            self.assertEqual(pub.sha256_bytes(t[pub.object_path(f["sha256"])]), f["sha256"])
        self.assertIn("releases/1.0.0/MedianXL-D2R-1.0.0.zip", t)
        sums = t["releases/1.0.0/SHA256SUMS"].decode().splitlines()
        self.assertIn(f"{pub.sha256_bytes(t[ch['manifest']])}  release-manifest.json", sums)
        self.assertIn(f"{man['zip']['sha256']}  MedianXL-D2R-1.0.0.zip", sums)
        self.assertTrue(all(not k.split("/")[-1].count(pub.PART) for k in t), "no temp files left")

    def test_object_dedup_inside_a_release_and_across_versions(self):
        t1 = Recorder(self.site)
        self.publish("1.0.0", target=t1)
        man = json.loads((self.site / "releases/1.0.0/release-manifest.json").read_text())
        self.assertGreater(len(man["files"]), len({f["sha256"] for f in man["files"]}), "fixture has duplicate bytes")
        self.assertEqual(len(t1.objects_put()), len({f["sha256"] for f in man["files"]}))
        # 1.1.0 changes one data file: the metadata.json (modVersion) differs anyway; everything else is shared
        t2 = Recorder(self.site)
        self.publish("1.1.0", target=t2, files={"global/excel/armor.txt": b"Median armor v2\n",
                                                "global/excel/weapons.txt": b"Median weapons\n"})
        self.assertEqual(len(t2.objects_put()), 2, t2.objects_put())  # new armor.txt + new metadata.json; dll, toml, weapons shared
        have = [p for p in tree(self.site) if p.startswith("objects/")]
        self.assertEqual(len(have), len(t1.objects_put()) + 2)

    def test_channel_is_written_last(self):
        t = Recorder(self.site)
        self.publish("1.0.0", "stable", target=t)
        self.assertEqual(t.puts[-1], "channels/stable.json")
        self.assertEqual(sum(1 for p in t.puts if p.startswith("channels/")), 1)
        i_man = t.puts.index("releases/1.0.0/release-manifest.json")
        self.assertTrue(all(t.puts.index(o) < i_man for o in t.objects_put()), "manifest after every object")
        self.assertLess(i_man, len(t.puts) - 1)

    def test_died_publish_shows_nothing_and_resumes(self):
        t = Recorder(self.site, fail_at=3)
        with self.assertRaises(pub.PublishError):
            self.publish("1.0.0", "stable", target=t)
        self.assertFalse((self.site / "channels").exists(), "no channel file after a failed publish")
        self.assertFalse((self.site / "releases/1.0.0/release-manifest.json").exists())
        t2 = Recorder(self.site)
        self.publish("1.0.0", "stable", target=t2)
        self.assertEqual(self.channel("stable")["version"], "1.0.0")
        total = len([p for p in tree(self.site) if p.startswith("objects/")])
        self.assertEqual(len(t2.objects_put()), total - 2, "the objects already uploaded were not sent again")

    def test_rerun_is_safe(self):
        dist = self.make_dist("1.0.0")
        t = pub.DirTarget(self.site)
        pub.publish(t, dist, "1.0.0", "stable", log=lambda *_: None)
        before = tree(self.site)
        r = Recorder(self.site)
        pub.publish(r, dist, "1.0.0", "stable", log=lambda *_: None)
        self.assertEqual(r.objects_put(), [])
        after = tree(self.site)
        changed = {k for k in before if before[k] != after[k]}
        self.assertLessEqual(changed, {"channels/stable.json"}, "only publishedAt can move")
        self.assertEqual(set(before), set(after))

    def test_published_version_never_changes(self):
        self.publish("1.0.0")
        with self.assertRaises(pub.PublishError):
            self.publish("1.0.0", files={"global/excel/armor.txt": b"other\n"})

    def test_wrong_size_object_is_replaced(self):
        self.publish("1.0.0")
        victim = next(p for p in self.site.glob("objects/*/*") if p.stat().st_size > 5)
        good = victim.read_bytes()
        victim.write_bytes(good[:3])
        r = Recorder(self.site)
        pub.publish(r, self.tmp / "dist-1.0.0", "1.0.0", "test", log=lambda *_: None)
        self.assertEqual(victim.read_bytes(), good)
        self.assertEqual(len(r.objects_put()), 1)

    def test_zip_content_must_match_the_manifest(self):
        dist = self.make_dist("1.0.0")
        man = json.loads((dist / "release-manifest.json").read_text())
        man["files"][0]["sha256"] = "0" * 64
        (dist / "release-manifest.json").write_text(json.dumps(man))
        with self.assertRaises(pub.PublishError):
            pub.publish(pub.DirTarget(self.site), dist, "1.0.0", "test", log=lambda *_: None)
        self.assertFalse((self.site / "channels").exists())

    def test_incomplete_package_is_not_for_stable(self):
        dist = self.make_dist("1.0.0", complete=False)
        t = pub.DirTarget(self.site)
        for kw in ({"channel": "stable", "allow_incomplete": True}, {"channel": "test"}):
            with self.assertRaises(pub.PublishError):
                pub.publish(t, dist, "1.0.0", log=lambda *_: None, **kw)
        pub.publish(t, dist, "1.0.0", "test", allow_incomplete=True, log=lambda *_: None)
        self.assertEqual(self.channel("test")["version"], "1.0.0")
        with self.assertRaises(pub.PublishError):
            pub.promote(t, "1.0.0", "stable", log=lambda *_: None)

    def test_wrong_version_is_refused(self):
        dist = self.make_dist("1.0.0")
        with self.assertRaises(pub.PublishError):
            pub.publish(pub.DirTarget(self.site), dist, "1.0.1", "test", log=lambda *_: None)

    def test_launcher_and_assets(self):
        exe = self.tmp / "launcher.exe"
        exe.write_bytes(b"MZ launcher")
        assets = self.tmp / "assets"
        assets.mkdir()
        (assets / "pack-1.zip").write_bytes(b"pack")
        self.publish("1.0.0", launcher=exe, launcher_version="0.3.0", assets=assets)
        t = tree(self.site)
        self.assertEqual(t["launcher/0.3.0/MedianXL-Launcher.exe"], b"MZ launcher")
        self.assertEqual(t["launcher/0.3.0/MedianXL-Launcher.exe.sha256"].decode().split()[0], pub.sha256_file(exe))
        self.assertEqual(t["assets/pack-1.zip"], b"pack")
        man = json.loads(t["releases/1.0.0/release-manifest.json"])
        self.assertEqual(man["launcher"]["path"], "launcher/0.3.0/MedianXL-Launcher.exe")
        self.assertEqual(man["launcher"]["sha256"], pub.sha256_file(exe))

    def test_publish_from_a_stage_folder(self):
        dist = self.make_dist("1.0.0")
        stage = self.tmp / "unzipped"
        import zipfile
        zipfile.ZipFile(dist / "MedianXL-D2R-1.0.0.zip").extractall(stage)
        pub.publish(pub.DirTarget(self.site), dist, "1.0.0", "test", source=pub.DirSource(stage / "MedianXL"), log=lambda *_: None)
        self.assertEqual(self.channel("test")["version"], "1.0.0")


class PromoteListGcTests(Base):
    def setUp(self):
        super().setUp()
        self.t = pub.DirTarget(self.site)
        # 1.0.0 stable, 1.1.0 test, 1.2.0 and 1.3.0 no channel; each version has data of its own plus shared files
        for i, v in enumerate(("1.0.0", "1.1.0", "1.2.0", "1.3.0")):
            files = {"global/excel/armor.txt": b"shared\n", f"global/excel/only-{v}.txt": f"only {v}\n".encode()}
            self.publish(v, {"1.0.0": "stable", "1.1.0": "test"}.get(v), files=files)

    def refs(self, v):
        return {f["sha256"] for f in json.loads((self.site / f"releases/{v}/release-manifest.json").read_text())["files"]}

    def objects(self):
        return {p.name for p in self.site.glob("objects/*/*")}

    def test_promote(self):
        pub.promote(self.t, "1.2.0", "stable", log=lambda *_: None)
        ch = self.channel("stable")
        self.assertEqual(ch["version"], "1.2.0")
        self.assertEqual(ch["manifestSha256"], pub.sha256_file(self.site / "releases/1.2.0/release-manifest.json"))
        with self.assertRaises(pub.PublishError):
            pub.promote(self.t, "9.9.9", "stable", log=lambda *_: None)
        victim = next(iter(self.refs("1.3.0") - self.refs("1.2.0")))
        (self.site / pub.object_path(victim)).unlink()
        with self.assertRaises(pub.PublishError):
            pub.promote(self.t, "1.3.0", "stable", log=lambda *_: None)
        self.assertEqual(self.channel("stable")["version"], "1.2.0", "a refused promote leaves the channel alone")

    def test_list(self):
        out = []
        versions, chans = pub.list_cmd(self.t, out.append)
        self.assertEqual(versions, ["1.0.0", "1.1.0", "1.2.0", "1.3.0"])
        self.assertEqual({k: v["version"] for k, v in chans.items()}, {"stable": "1.0.0", "test": "1.1.0"})
        self.assertTrue(any("1.0.0" in ln and "stable" in ln for ln in out))

    def test_gc_never_touches_channel_versions(self):
        expect = self.refs("1.2.0") - self.refs("1.0.0") - self.refs("1.1.0") - self.refs("1.3.0")
        doomed, orphans = pub.gc(self.t, 1, log=lambda *_: None)
        self.assertEqual(doomed, ["1.2.0"])  # kept: newest 1.3.0 + stable 1.0.0 + test 1.1.0
        self.assertEqual(pub.release_versions(self.t), ["1.0.0", "1.1.0", "1.3.0"])
        for v in ("1.0.0", "1.1.0", "1.3.0"):
            self.assertTrue(self.refs(v) <= self.objects(), f"every object of {v} is still there")
        self.assertEqual(self.objects(), self.refs("1.0.0") | self.refs("1.1.0") | self.refs("1.3.0"))
        self.assertEqual({Path(rel).name for rel, _ in orphans}, expect)
        self.assertTrue(len(expect) >= 1)
        self.assertEqual(self.channel("stable")["version"], "1.0.0")

    def test_gc_keep_two(self):
        pub.gc(self.t, 2, log=lambda *_: None)
        self.assertEqual(pub.release_versions(self.t), ["1.0.0", "1.1.0", "1.2.0", "1.3.0"])  # 1.2.0 + 1.3.0 newest two
        pub.promote(self.t, "1.3.0", "stable", log=lambda *_: None)
        pub.promote(self.t, "1.3.0", "test", log=lambda *_: None)
        pub.gc(self.t, 1, log=lambda *_: None)
        self.assertEqual(pub.release_versions(self.t), ["1.3.0"])
        self.assertEqual(self.objects(), self.refs("1.3.0"))

    def test_gc_dry_run_deletes_nothing(self):
        before = tree(self.site)
        pub.gc(self.t, 1, dry_run=True, log=lambda *_: None)
        self.assertEqual(tree(self.site), before)

    def test_gc_refuses_when_a_channel_points_nowhere(self):
        shutil.rmtree(self.site / "releases/1.0.0")
        before = tree(self.site)
        with self.assertRaises(pub.PublishError):
            pub.gc(self.t, 1, log=lambda *_: None)
        self.assertEqual(tree(self.site), before)

    def test_gc_sees_every_channel_file(self):
        extra = dict(self.channel("stable"), version="1.2.0", manifest="releases/1.2.0/release-manifest.json")
        (self.site / "channels/beta.json").write_text(json.dumps(extra))
        pub.gc(self.t, 1, log=lambda *_: None)
        self.assertIn("1.2.0", pub.release_versions(self.t))

    def test_gc_removes_unfinished_publishes_and_temp_files(self):
        (self.site / "releases/1.4.0").mkdir()
        (self.site / "releases/1.4.0/MedianXL-D2R-1.4.0.zip").write_bytes(b"half")  # a publish that died before its manifest
        stale = self.site / "objects/ab" / ("c" * 64 + pub.PART + "1234")
        stale.parent.mkdir(exist_ok=True)
        stale.write_bytes(b"x")
        pub.gc(self.t, 1, log=lambda *_: None)
        self.assertFalse((self.site / "releases/1.4.0").exists())
        self.assertFalse(stale.exists())
        self.assertIn("1.3.0", pub.release_versions(self.t))

    def test_gc_keeps_a_launcher_still_in_use(self):
        exe = self.tmp / "l.exe"
        exe.write_bytes(b"L")
        self.publish("1.4.0", files={"global/excel/armor.txt": b"x\n"}, launcher=exe, launcher_version="0.9.0")
        self.publish("1.5.0", files={"global/excel/armor.txt": b"y\n"}, launcher=exe, launcher_version="0.9.0")
        pub.gc(self.t, 1, log=lambda *_: None)
        self.assertTrue((self.site / "launcher/0.9.0/MedianXL-Launcher.exe").exists())


class VersionTests(unittest.TestCase):
    def test_order_and_validation(self):
        vs = ["0.70.0", "0.9.0", "0.70.1", "1.0.0-rc1", "1.0.0", "0.100.0"]
        self.assertEqual(sorted(vs, key=pub.version_key), ["0.9.0", "0.70.0", "0.70.1", "0.100.0", "1.0.0-rc1", "1.0.0"])
        self.assertEqual(pub.check_version("v0.70.0"), "0.70.0")
        for bad in ("../x", "1.2", "1.2.3/../../x", "latest", ""):
            with self.assertRaises(SystemExit):
                pub.check_version(bad)

    def test_sftp_target_parse(self):
        t = pub.SftpTarget.parse("sftp://upload@dlhost:2222/mxl/")
        self.assertEqual((t.user, t.host, t.port, t.path), ("upload", "dlhost", 2222, "/mxl"))
        t = pub.SftpTarget.parse("sftp://u@h/~/site")
        self.assertEqual((t.port, t.path), (None, "site"))
        self.assertEqual(t._abs("objects/ab"), "site/objects/ab")
        with self.assertRaises(SystemExit):
            pub.SftpTarget.parse("ftp://x/y")

    def test_ls_parse(self):
        lines = ["-rw-r--r--    1 1000     1000       123456 Oct 26 10:00 abc",
                 "drwxr-xr-x    2 0        0            4096 Oct 26 2025 a dir",
                 "-rw-r--r--    1 0        0               0 Jan  1 00:00 ."]
        self.assertEqual(pub.parse_sftp_ls(lines), {"abc": (123456, False), "a dir": (4096, True)})


class SftpTests(Base):
    def target(self):
        return pub.SftpTarget("upload", "example.invalid", 2222, "/mxl", sftp_cmd=FAKE_SFTP)

    def setUp(self):
        super().setUp()
        (self.site / "mxl").mkdir()  # the account's folder exists on the server (docs/hosting.md)
        p = mock.patch.dict(os.environ, {"FAKE_SFTP_ROOT": str(self.site)})
        p.start()
        self.addCleanup(p.stop)

    def test_same_result_as_a_dir_target(self):
        ref = self.tmp / "ref"
        ref.mkdir()
        dist = self.make_dist("1.0.0")
        pub.publish(pub.DirTarget(ref), dist, "1.0.0", "stable", log=lambda *_: None)
        pub.publish(self.target(), dist, "1.0.0", "stable", log=lambda *_: None)
        a, b = tree(ref), tree(self.site / "mxl")
        self.assertEqual(set(a), set(b))
        for k in a:
            if k != "channels/stable.json":
                self.assertEqual(a[k], b[k], k)
        self.assertEqual(json.loads(b["channels/stable.json"])["manifestSha256"], json.loads(a["channels/stable.json"])["manifestSha256"])

    def test_server_without_posix_rename(self):
        dist = self.make_dist("1.0.0")
        with mock.patch.dict(os.environ, {"FAKE_SFTP_LEGACY_RENAME": "1"}):
            pub.publish(self.target(), dist, "1.0.0", "test", log=lambda *_: None)
            first = (self.site / "mxl/channels/test.json").read_text()
            pub.publish(self.target(), self.make_dist("1.0.1"), "1.0.1", "test", log=lambda *_: None)  # channel replaced
        self.assertEqual(json.loads((self.site / "mxl/channels/test.json").read_text())["version"], "1.0.1")
        self.assertEqual(json.loads(first)["version"], "1.0.0")
        self.assertFalse([p for p in tree(self.site) if pub.PART in p], "no temp files left")

    def test_second_run_uploads_no_objects(self):
        dist = self.make_dist("1.0.0")
        pub.publish(self.target(), dist, "1.0.0", "test", log=lambda *_: None)
        log = self.tmp / "sftp.log"
        with mock.patch.dict(os.environ, {"FAKE_SFTP_LOG": str(log)}):
            pub.publish(self.target(), dist, "1.0.0", "test", log=lambda *_: None)
        puts = [ln for ln in log.read_text().splitlines() if ln.startswith("put ")]
        self.assertFalse([p for p in puts if "/objects/" in p], puts)

    def test_died_session_leaves_no_channel(self):
        dist = self.make_dist("1.0.0")
        with mock.patch.dict(os.environ, {"FAKE_SFTP_FAIL_PUT": "4"}):
            with self.assertRaises(pub.PublishError):
                pub.publish(self.target(), dist, "1.0.0", "stable", log=lambda *_: None)
        self.assertFalse((self.site / "mxl/channels").exists())
        pub.publish(self.target(), dist, "1.0.0", "stable", log=lambda *_: None)
        self.assertEqual(json.loads((self.site / "mxl/channels/stable.json").read_text())["version"], "1.0.0")

    def test_list_and_gc_over_sftp(self):
        for v, ch in (("1.0.0", "stable"), ("1.1.0", None), ("1.2.0", None)):
            self.publish(v, ch, target=self.target(), files={"global/excel/armor.txt": v.encode()})
        t = self.target()
        out = []
        versions, chans = pub.list_cmd(t, out.append)
        self.assertEqual(versions, ["1.0.0", "1.1.0", "1.2.0"])
        self.assertEqual(chans["stable"]["version"], "1.0.0")
        pub.gc(t, 1, log=lambda *_: None)
        self.assertEqual(pub.release_versions(t), ["1.0.0", "1.2.0"])
        self.assertIsNone(t.list_dir("releases/1.1.0"))
        self.assertEqual(t.read("channels/nothing.json"), None)

    def test_listing_error_is_not_mistaken_for_an_empty_folder(self):
        t = self.target()
        with mock.patch.object(pub.SftpTarget, "_run", return_value=([], "Couldn't read directory: Permission denied")):
            with self.assertRaises(pub.PublishError):
                t.list_dir("objects/ab")


class ServeAndVerifyTests(Base):
    def get(self, url, headers=None, method="GET"):
        req = urllib.request.Request(url, headers=headers or {}, method=method)
        try:
            with urllib.request.urlopen(req) as r:
                return r.status, dict(r.headers), r.read()
        except urllib.error.HTTPError as e:
            return e.code, dict(e.headers), e.read()

    def test_range_server(self):
        (self.site / "f.bin").write_bytes(bytes(range(100)))
        (self.site / "objects").mkdir()
        (self.site / "objects" / "o").write_bytes(b"x")
        with serve_dir.running(self.site) as base:
            st, h, body = self.get(base + "f.bin")
            self.assertEqual((st, len(body), h["Accept-Ranges"]), (200, 100, "bytes"))
            self.assertEqual(h["Cache-Control"], "no-cache")
            st, h, body = self.get(base + "f.bin", {"Range": "bytes=10-19"})
            self.assertEqual((st, body, h["Content-Range"]), (206, bytes(range(10, 20)), "bytes 10-19/100"))
            st, h, body = self.get(base + "f.bin", {"Range": "bytes=90-"})
            self.assertEqual((st, body), (206, bytes(range(90, 100))))
            st, h, body = self.get(base + "f.bin", {"Range": "bytes=-5"})
            self.assertEqual((st, body, h["Content-Range"]), (206, bytes(range(95, 100)), "bytes 95-99/100"))
            st, h, body = self.get(base + "f.bin", {"Range": "bytes=50-500"})
            self.assertEqual((st, len(body), h["Content-Range"]), (206, 50, "bytes 50-99/100"))
            self.assertEqual(self.get(base + "f.bin", {"Range": "bytes=100-"})[0], 416)
            st, h, body = self.get(base + "f.bin", method="HEAD")
            self.assertEqual((st, body, h["Content-Length"]), (200, b"", "100"))
            self.assertEqual(self.get(base + "missing")[0], 404)
            self.assertEqual(self.get(base + "objects/")[0], 404, "no directory listing")
            self.assertIn("immutable", self.get(base + "objects/o")[1]["Cache-Control"])
            st, h, _ = self.get(base + "f.bin")
            self.assertEqual(self.get(base + "f.bin", {"If-None-Match": h["ETag"]})[0], 304)
            self.assertEqual(self.get(base + "..%2f..%2fetc/passwd")[0], 404)
            self.assertEqual(self.get(base + "%2e%2e/x")[0], 404)

    def test_end_to_end_publish_serve_verify(self):
        """publish -> HTTP (Range capable) -> channel, manifest, every object checked by sha256, as the launcher will."""
        self.publish("1.0.0", "stable")
        self.publish("1.1.0", "test", files={"global/excel/armor.txt": b"new\n"})
        with serve_dir.running(self.site) as base:
            n = pub.verify_url(base, "stable", log=lambda *_: None)
            self.assertGreaterEqual(n, 4)
            pub.verify_url(base, "test", log=lambda *_: None)
            # what the launcher does by hand
            ch = json.loads(urllib.request.urlopen(base + "channels/stable.json").read())
            raw = urllib.request.urlopen(base + ch["manifest"]).read()
            self.assertEqual(pub.sha256_bytes(raw), ch["manifestSha256"])
            man = json.loads(raw)
            for f in man["files"]:
                body = urllib.request.urlopen(base + man["objectsBase"] + f["sha256"][:2] + "/" + f["sha256"]).read()
                self.assertEqual(pub.sha256_bytes(body), f["sha256"], f["path"])
                self.assertEqual(len(body), f["size"])
            zbody = urllib.request.urlopen(base + man["zip"]["path"]).read()
            self.assertEqual(pub.sha256_bytes(zbody), man["zip"]["sha256"])
            # a corrupted object is caught
            obj = self.site / pub.object_path(man["files"][0]["sha256"])  # an object of the stable release
            obj.write_bytes(obj.read_bytes() + b"!")
            with self.assertRaises(pub.PublishError):
                pub.verify_url(base, "stable", log=lambda *_: None)

    def test_end_to_end_with_a_real_package_release_run(self):
        """package_release.py --no-data (what CI does) -> publish --allow-incomplete -> serve -> verify."""
        out = self.tmp / "dist"
        r = subprocess.run([sys.executable, "-I", str(HERE / "package_release.py"), "--no-data", "--plugin", str(self.dll),
                            "--version", "0.0.1", "--out", str(out)], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        argv = ["publish", "--version", "0.0.1", "--channel", "test", "--dist", str(out), "--allow-incomplete",
                "--target", f"dir:{self.site}"]
        self.assertEqual(pub.main(argv), 0)
        self.assertEqual(pub.main(argv), 0, "second run")
        self.assertEqual(pub.main(["list", "--target", f"dir:{self.site}"]), 0)
        with serve_dir.running(self.site) as base:
            self.assertEqual(pub.main(["verify", "--url", base, "--channel", "test"]), 0)
        self.assertEqual(pub.main(["promote", "0.0.1", "--channel", "stable", "--target", f"dir:{self.site}"]), 1,
                         "incomplete -> not stable")
        self.assertEqual(pub.main(["gc", "--keep", "1", "--target", f"dir:{self.site}"]), 0)


if __name__ == "__main__":
    unittest.main()
