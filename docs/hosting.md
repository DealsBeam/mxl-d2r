# Download host (dl.werdegames.com/mxl)

Where players' downloaders get the mod from (owner decision 2026-10-26: our own server, not GitHub; the forge needs a sign-in
for everything). The host is **a folder served by Caddy `file_server`**, plus one small script that runs there when an
upload arrives. We upload over SFTP with `tools/publish_release.py`; players read the folder over HTTPS. What is in the
folder and how the downloader uses it: [release.md](release.md) "Hosting layout".

```
Developer PC --sftp (key, chroot)--> /srv/dl/incoming/<v>/ --dl_ingest.py (systemd, as mxlup)--> /srv/dl/mxl/ <--Caddy--HTTPS-- downloader
```

Why the extra step: the uploader's line is slow (about 160 KB/s up) and each SSH login takes seconds, so the publisher
sends only the objects the host lacks, packed into one 7z, and the server unpacks and checks them, builds the players'
7z itself (355 MB that never cross the slow line) and switches the channel last. (The Caddy proxy in front of the forge
drops long HTTP uploads, which is why we upload over SSH and not HTTP.)

Players download from a mirror of that folder on a DigitalOcean Space behind Cloudflare,
`https://files.werdegames.com/mxl/` (section 7); the server keeps it in step after every change, and `dl.werdegames.com`
stays the origin and backup.

Everything below is for the server's owner. `HOST` is the server (the one behind git.werdegames.com). Names used: user
`mxlup`, folders `/srv/dl/mxl` (served) and `/srv/dl/incoming` (not served), URL `https://dl.werdegames.com/mxl/`;
change them as you like, keep them consistent.

## 1. DNS

Create `dl.werdegames.com` (A and AAAA, the same addresses as the forge) at the DNS provider. Caddy gets the certificate
by itself once the name resolves and ports 80 and 443 reach Caddy.

## 2. Folders, upload user, permissions

```
/srv/dl                    root:root   0755   the SFTP chroot; must belong to root and not be writable by anyone else
/srv/dl/mxl                mxlup:mxlup 0755   the site root (served)
  channels/ releases/ objects/ launcher/ assets/     written by dl_ingest.py and publish_release.py (files 0644, folders 0755)
/srv/dl/incoming           mxlup:mxlup 0755   uploads waiting for dl_ingest.py (not served)
/opt/mxl-dl/dl_ingest.py   root:root   0755   the server script (mxlup can run it, not change it)
```

As root on the server (Debian/Ubuntu names; adapt for others):

```sh
apt install python3 7zip          # 7zip gives 7zz (older releases: 7z from p7zip-full also works); python3 3.9 or newer
useradd --no-create-home --shell /usr/sbin/nologin mxlup      # skip if mxlup exists already
usermod -p '*' mxlup              # no password at all, but not "locked" (a locked account is refused even with a key)
install -d -m 0755 -o root  -g root  /srv/dl
install -d -m 0755 -o mxlup -g mxlup /srv/dl/mxl /srv/dl/incoming
install -d -m 0755 /etc/ssh/authorized_keys.d /opt/mxl-dl
```

The key is made on the machine that publishes (Windows has OpenSSH built in; PowerShell):

```powershell
ssh-keygen -t ed25519 -f $HOME\.ssh\mxl_publish -C mxl-publish     # empty passphrase for an unattended run, or use ssh-agent
```

Put the **public** key (`mxl_publish.pub`, one line) on the server, with `restrict` in front so the key can do nothing but
the SFTP session the config below allows:

```sh
echo 'restrict ssh-ed25519 AAAA...the-rest-of-the-line... mxl-publish' > /etc/ssh/authorized_keys.d/mxlup
chmod 0644 /etc/ssh/authorized_keys.d/mxlup
```

Add to `/etc/ssh/sshd_config` (at the end; a `Match` block runs to the end of the file or the next `Match`):

```
Match User mxlup
    ChrootDirectory /srv/dl
    ForceCommand internal-sftp -u 022
    AuthorizedKeysFile /etc/ssh/authorized_keys.d/%u
    PasswordAuthentication no
    KbdInteractiveAuthentication no
    AllowTcpForwarding no
    AllowAgentForwarding no
    X11Forwarding no
    PermitTTY no
    PermitTunnel no
```

`internal-sftp` runs inside sshd, so the chroot needs no `/bin`, `/lib` or `/dev`. `-u 022` makes new files 0644 / folders 0755
so Caddy (another user) can read them. Inside the chroot the upload user sees `/mxl` and `/incoming`. Then:

```sh
sshd -t && systemctl reload ssh        # the unit is called sshd on some systems
```

If the forge's own SSH server already uses port 22, either the system sshd listens on another port (put `:port` in the
target: `sftp://mxlup@HOST:2222/mxl`) or this user is served by a second sshd instance. Check with `ss -ltnp | grep ssh`.

Check from the publishing PC (accept the host key on the first connection, so later batch runs do not stop at the question):

```powershell
sftp -i $HOME\.ssh\mxl_publish mxlup@HOST         # prompt "sftp>": `ls /` shows incoming and mxl, `cd /..` stays inside the chroot
ssh  -i $HOME\.ssh\mxl_publish mxlup@HOST         # must be refused ("This service allows sftp connections only.")
```

## 3. The ingest script and its systemd units

`tools/dl_ingest.py` (one file, Python standard library and 7-Zip only) checks an upload in `/srv/dl/incoming/<v>/`, moves
its objects into `/srv/dl/mxl/objects/`, builds `releases/<v>/D2R-XL-<v>.7z` from the objects, writes the release,
the downloader and the channel file (last), then deletes the upload. Anything that fails a check is refused and left in
place with a `REFUSED` file (the reason). Details: release.md "Publishing". It writes nothing outside `/srv/dl`.

Copy it from the repository to the server with your own admin login (not `mxlup`), for example from the PC:

```powershell
scp tools/dl_ingest.py root@HOST:/opt/mxl-dl/dl_ingest.py
```

then on the server, as root:

```sh
chown root:root /opt/mxl-dl/dl_ingest.py && chmod 0755 /opt/mxl-dl/dl_ingest.py

cat > /etc/systemd/system/mxl-dl-ingest.service <<'EOF'
[Unit]
Description=D2R-XL downloads: check and publish uploads in /srv/dl/incoming (dl_ingest.py)

[Service]
Type=oneshot
User=mxlup
Group=mxlup
UMask=0022
ExecStart=/usr/bin/python3 -I /opt/mxl-dl/dl_ingest.py --root /srv/dl
Nice=10
IOSchedulingClass=idle
TimeoutStartSec=3h
ProtectSystem=strict
ReadWritePaths=/srv/dl
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
PrivateNetwork=yes
NoNewPrivileges=yes
CapabilityBoundingSet=
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
RestrictAddressFamilies=AF_UNIX
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
SystemCallArchitectures=native
EOF

cat > /etc/systemd/system/mxl-dl-ingest.path <<'EOF'
[Unit]
Description=D2R-XL downloads: watch /srv/dl/incoming for finished uploads

[Path]
PathChanged=/srv/dl/incoming
Unit=mxl-dl-ingest.service

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/mxl-dl-ingest.timer <<'EOF'
[Unit]
Description=D2R-XL downloads: look at /srv/dl/incoming every 15 minutes too (an upload that finished during a run)

[Timer]
OnBootSec=5min
OnUnitInactiveSec=15min
Unit=mxl-dl-ingest.service

[Install]
WantedBy=timers.target
EOF

systemctl daemon-reload
systemctl enable --now mxl-dl-ingest.path mxl-dl-ingest.timer
systemctl start mxl-dl-ingest.service && journalctl -u mxl-dl-ingest -n 20 --no-pager    # an empty run: no errors
```

How it fits together: the publisher uploads into `incoming/<v>.uploading/`, writes `READY` last and renames the folder
to `incoming/<v>/`; the path unit sees that rename and starts the service. The script runs again until nothing is left,
takes a lock (two starts at once are harmless) and keeps its scratch files inside the upload folder, so it does not
trigger itself. Building the players' 7z of a full release takes a few minutes of CPU (it runs at low priority).

Updating the script later: copy the new `dl_ingest.py` the same way; no restart needed (each run starts it anew).

Useful commands on the server:

```sh
journalctl -u mxl-dl-ingest -n 50 --no-pager                  # what the last runs did; refusals are shown as errors
ls -la /srv/dl/incoming                                       # uploads waiting, or refused (with a REFUSED file)
cat /srv/dl/incoming/<v>/REFUSED                              # why
sudo -u mxlup python3 -I /opt/mxl-dl/dl_ingest.py --root /srv/dl --retry <v>    # try a refused upload again
```

A refused upload is also reported by the next `publish` of that version on the PC; `publish --force` deletes it there and
uploads again.

**Leftovers of the first (failed) publish**: the old publisher wrote objects straight into `/srv/dl/mxl/objects/`. Complete
objects stay and are reused (the next publish does not send them again, and the ingest hashes every object again before it
builds a 7z, refusing a damaged one). Temp files (`*.part-*`) older than an hour are deleted by every ingest run; nothing
to do by hand.

## 4. Caddy

In the server's Caddyfile (Caddy 2). The host serves `/srv/dl`, and only the `/mxl/` part of it (never `/incoming/`):

```
dl.werdegames.com {
	root * /srv/dl

	# Caddy compresses what is worth it; the downloader checks sha256 of the decoded bytes.
	encode zstd gzip {
		match {
			header Content-Type application/json*
			header Content-Type text/*
		}
	}

	# objects/<sha256[0:2]>/<sha256> never change: cache forever
	header /mxl/objects/* Cache-Control "public, max-age=31536000, immutable"
	# the channel files are what the downloader polls: always revalidate
	header /mxl/channels/* Cache-Control "no-cache"
	# a published version does not change either, but allow a rare --force: one hour
	header /mxl/releases/* Cache-Control "public, max-age=3600"
	header /mxl/launcher/* Cache-Control "public, max-age=3600"
	header /mxl/assets/* Cache-Control "public, max-age=3600"

	handle /mxl/* {
		# never served: half-written files (*.part-<hex>) and dot files
		@hidden path_regexp hidden (?:/\.|\.part-[0-9a-z]+$)
		respond @hidden 404
		file_server        # Range requests, ETag, Last-Modified; no "browse" = no folder listings
	}
	handle {
		respond 404
	}
}
```

Notes:
- **CORS is not needed**: the downloader is a program, not a web page. No `Access-Control-*` headers.
- Objects have no file extension, so Caddy sniffs their type: text tables come out as `text/*` and are compressed, binaries
  (dds, models, the dll) are not. If `publish_release.py verify` (below) shows `Range requests` failing, or a
  `Content-Encoding` on a 206 answer, drop the `text/*` line from `match`: resumable downloads win over a few MB.
- `Cache-Control` of `releases/`, `launcher/`, `assets/` is deliberately short; only `objects/` is immutable.
- Reload: `caddy validate --config /etc/caddy/Caddyfile && systemctl reload caddy`. Caddy needs read access to `/srv/dl/mxl`
  (0755 / 0644 as above; `ls -l` should show world-readable).
- Serve nothing else from this site name: the last `handle` answers 404 for every other path (also `/incoming/`); the forge
  keeps its own name.
- `header` also stamps error answers, so a request for an object that is not uploaded yet gets `immutable` on its 404. The
  downloader only asks for objects a published manifest names, which are all there, so this only matters for a browser or a CDN.

## 5. Publishing, checks

On the PC with the finished `dist/` from `tools/package_release.py` (docs/release.md "Making a release"; 7-Zip installed):

```powershell
$t = "sftp://mxlup@HOST/mxl"       # add :port when sshd is not on 22
$k = "$HOME\.ssh\mxl_publish"
py -3 tools/publish_release.py list --target $t --identity $k           # channels, releases, uploads waiting in incoming/
py -3 tools/publish_release.py publish --version 0.70.0 --channel test --target $t --identity $k
py -3 tools/publish_release.py list --target $t --identity $k           # a minute or a few later: test -> 0.70.0
py -3 tools/publish_release.py verify  --url https://dl.werdegames.com/mxl/ --channel test   # every object + the 7z over HTTPS, sha256
curl.exe -sI https://dl.werdegames.com/mxl/channels/test.json                                # Cache-Control: no-cache
curl.exe -sI https://dl.werdegames.com/mxl/objects/ab/<some sha256>                          # ... immutable, Accept-Ranges: bytes
py -3 tools/publish_release.py promote 0.70.0 --channel stable --target $t --identity $k
```

`MXL_PUBLISH_TARGET` in the environment replaces `--target`. `publish` needs two SFTP logins: one to see what the host has,
one to upload. It sends one 7z of the objects the host lacks (a first full release: about 1.2 GB of files, a few hundred
MB packed; a release that changes a few tables: a few MB), the manifest, the notes and the downloader if given. A dropped
connection is retried and the 7z resumed where it stopped; when it gives up, running the same command again resumes too.
Nothing is visible to players until the server has checked everything and written the channel file.

Maintenance:
- `list` shows channels, releases and uploads in `incoming/` (waiting or refused). `gc --keep 3 --dry-run`, then
  `gc --keep 3`: removes the older releases (their 7z too) and the objects nothing else uses; releases a channel points to
  are never removed. Do not run `gc` while an upload waits in `incoming/` (it could delete objects that upload counts on;
  the ingest would then refuse it with "objects the host does not have", and publishing again fixes it).
- Disk: one 7z (0.36 GB) per kept release plus the objects of all kept releases (shared), plus room for one upload and the
  7z being built.
- Backup: the folder is rebuildable from `dist/` of each release (7z, manifest); keep those.
- Rolling back: `promote <older version> --channel stable` (the older release must still be on the host).
- Offline test of the same layout, no server: a folder `X` with `X/mxl` and `X/incoming`: `publish --target dir:X/mxl`,
  `py -3 tools/dl_ingest.py --root X`, `py -3 tools/serve_dir.py X/mxl`, then `verify --url http://127.0.0.1:<port>/`.

## 6. What the owner has to do, in order

1. DNS: `dl.werdegames.com` -> the server.
2. On the server: section 2 (`apt install python3 7zip`, user, folders including `/srv/dl/incoming`, key line,
   `sshd_config` block, `sshd -t && reload`).
3. On the server: section 3 (copy `dl_ingest.py` to `/opt/mxl-dl/`, the three unit files, `systemctl enable --now`, the
   empty test run).
4. On the server: the Caddyfile block of section 4, `caddy validate`, reload.
5. On the PC: generate the key, put `mxl_publish.pub` on the server (step 2), `sftp`/`ssh` checks of section 2 (accepts the
   host key).
6. Run section 5 once with a `--no-data` package or the first real release on `test`; `verify` must end with
   `N of N objects verified`, `Range requests: ok (206)` and the 7z's `sha256 ok`.
7. Tell the coordinator the root URL, and whether sshd is on port 22 (the downloader takes the root URL as its setting).
8. DigitalOcean Spaces + Cloudflare (section 7): console steps, then on the server rclone, `rclone.conf`, `dl_sync.py`
   and the new `dl_ingest.py`, the three `mxl-dl-sync` units, the first sync and `--check`; from the PC the `curl.exe -sI`
   checks and `verify --url https://files.werdegames.com/mxl/`.

## 7. DigitalOcean Spaces + Cloudflare (files.werdegames.com)

Owner decision 2026-10-27: players download from a DigitalOcean Space behind Cloudflare. The pipeline above stays as it
is (SFTP to `incoming/`, `dl_ingest.py` checks and builds, `/srv/dl/mxl/` is the origin and backup at
`https://dl.werdegames.com/mxl/`); after every change the server mirrors `/srv/dl/mxl/` to the Space under the prefix
`mxl/`, so `https://files.werdegames.com/mxl/` has the identical layout (channels/, releases/, objects/, launcher/,
assets/).

```
dl_ingest.py / promote / gc --write--> /srv/dl/mxl/.sync/wanted --mxl-dl-sync.path--> dl_sync.py (rclone, as mxlup)
    --> Space d2r-xl, prefix mxl/ --Spaces CDN--> Cloudflare (files.werdegames.com) --> downloader
```

`tools/dl_sync.py` uploads in a safe order: objects first, then releases (7z, notes, sums), launcher, assets, the
release manifests, **the channel files last**, and deletions (what `gc` removed on the server) only after all of that
went through, never an object a release on the server still uses, and never more than half the Space at once without
`--allow-mass-delete`. Only new or changed files go up (objects: by name and size; other files: by sha256 against the
manifest of the last upload, `/var/lib/mxl-dl-sync/uploaded.json`). Every file gets the public-read ACL and these
headers (the same as Caddy's):

| Prefix | Cache-Control | Content-Type |
|---|---|---|
| `objects/` | `public, max-age=31536000, immutable` | `application/octet-stream` |
| `channels/` | `no-cache` | `application/json` |
| `releases/`, `launcher/`, `assets/` | `public, max-age=3600` | by extension: `.json` application/json, `.7z` application/x-7z-compressed, `.exe` application/octet-stream, `.md` text/markdown, `SHA256SUMS` / `.sha256` text/plain |

Never uploaded: dot files and folders (`.sync/`), temp files (`*.part-*`), anything outside those five folders. The
ingest unit keeps `PrivateNetwork=yes`: it only writes the trigger file; the network belongs to the sync unit.

### Console steps (owner)

1. DigitalOcean: Space `d2r-xl`, region fra1, **File listing: Restricted**; Spaces CDN on, custom subdomain
   `files.werdegames.com` with the Cloudflare Origin CA certificate uploaded to DigitalOcean. CDN edge cache TTL: the
   lowest value offered (see "Stale channels" below).
2. DigitalOcean: a Spaces access key limited to bucket `d2r-xl` (read/write/delete). Shown once; it goes straight into
   the server's rclone.conf below, nowhere else.
3. Cloudflare: DNS `CNAME files -> d2r-xl.fra1.cdn.digitaloceanspaces.com`, proxied; a configuration rule for
   `files.werdegames.com`: SSL **Full (strict)**; a cache rule for the same host: **Eligible for cache**, edge TTL
   **use the origin's Cache-Control**. If `verify` below gets HTTP 403 from Cloudflare, turn off Browser Integrity Check
   for this host in the configuration rule (the downloader is not a browser).

### Server steps (as root)

```sh
apt install rclone                 # Debian 12: 1.60 (--check reads headers with 1.59 or newer; older: the .deb from rclone.org)
install -d -m 0750 -o root -g mxlup /etc/mxl-dl
install -m 0640 -o root -g mxlup /dev/null /etc/mxl-dl/rclone.conf
cat > /etc/mxl-dl/rclone.conf <<'CONF'
[spaces]
type = s3
provider = DigitalOcean
env_auth = false
access_key_id = PUT_ACCESS_KEY_ID_HERE
secret_access_key = PUT_SECRET_KEY_HERE
endpoint = fra1.digitaloceanspaces.com
acl = public-read
CONF
nano /etc/mxl-dl/rclone.conf       # replace the two PUT_..._HERE values with the key (paste in nano, never on a command line)
ls -l /etc/mxl-dl/rclone.conf      # -rw-r----- root mxlup
sudo -u mxlup rclone --config /etc/mxl-dl/rclone.conf --s3-no-check-bucket lsf spaces:d2r-xl   # no error (empty at first)
install -d -m 0755 -o mxlup -g mxlup /srv/dl/mxl/.sync     # trigger + status; a dot folder: Caddy answers 404 for it
```

Copy the script from the PC (as for `dl_ingest.py`): `scp tools/dl_sync.py root@HOST:/opt/mxl-dl/dl_sync.py`, and the
updated `tools/dl_ingest.py` the same way (it writes the trigger). Then:

```sh
chown root:root /opt/mxl-dl/dl_sync.py /opt/mxl-dl/dl_ingest.py && chmod 0755 /opt/mxl-dl/dl_sync.py /opt/mxl-dl/dl_ingest.py

cat > /etc/systemd/system/mxl-dl-sync.service <<'EOF'
[Unit]
Description=D2R-XL downloads: mirror /srv/dl/mxl to the DigitalOcean Space (dl_sync.py)
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
User=mxlup
Group=mxlup
UMask=0022
StateDirectory=mxl-dl-sync
StateDirectoryMode=0750
ExecStart=/usr/bin/python3 -I /opt/mxl-dl/dl_sync.py --site /srv/dl/mxl --remote spaces:d2r-xl/mxl --config /etc/mxl-dl/rclone.conf
Nice=5
IOSchedulingClass=idle
TimeoutStartSec=6h
ProtectSystem=strict
ReadOnlyPaths=/srv/dl/mxl
ReadWritePaths=/srv/dl/mxl/.sync
ProtectHome=yes
PrivateTmp=yes
PrivateDevices=yes
NoNewPrivileges=yes
CapabilityBoundingSet=
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes
ProtectControlGroups=yes
ProtectClock=yes
ProtectHostname=yes
RestrictAddressFamilies=AF_UNIX AF_INET AF_INET6
RestrictNamespaces=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
LockPersonality=yes
SystemCallArchitectures=native
EOF

cat > /etc/systemd/system/mxl-dl-sync.path <<'EOF'
[Unit]
Description=D2R-XL downloads: mirror to the Space when /srv/dl/mxl/.sync/wanted changes (ingest, promote, gc)

[Path]
PathChanged=/srv/dl/mxl/.sync/wanted
Unit=mxl-dl-sync.service

[Install]
WantedBy=multi-user.target
EOF

cat > /etc/systemd/system/mxl-dl-sync.timer <<'EOF'
[Unit]
Description=D2R-XL downloads: mirror to the Space every 30 minutes too (safety net)

[Timer]
OnBootSec=10min
OnUnitInactiveSec=30min
Unit=mxl-dl-sync.service

[Install]
WantedBy=timers.target
EOF

systemctl daemon-reload
install -d -m 0750 -o mxlup -g mxlup /var/lib/mxl-dl-sync              # systemd would make it too; needed for the dry run
sudo -u mxlup python3 -I /opt/mxl-dl/dl_sync.py --dry-run               # what the first sync will upload
systemctl enable --now mxl-dl-sync.path mxl-dl-sync.timer
systemctl start mxl-dl-sync.service          # the first full sync (objects + 7z of every kept release; minutes from the server)
journalctl -u mxl-dl-sync -n 30 --no-pager   # ends with "done: N uploaded (... MB), 0 deleted"
sudo -u mxlup python3 -I /opt/mxl-dl/dl_sync.py --check          # every file there with its size; 50 files + channels by sha256 and headers
sudo -u mxlup python3 -I /opt/mxl-dl/dl_sync.py --check --full   # every file by sha256 (downloads everything once)
```

Who triggers a sync: `dl_ingest.py` after it published a version (it has no network; it only writes
`/srv/dl/mxl/.sync/wanted`), and `publish_release.py promote`, `gc` and an `--assets` upload, which write the same file
over SFTP; the path unit starts the service. A change during a run makes it run once more; the timer catches anything
else. The lock `/var/lib/mxl-dl-sync/lock` keeps a manual run and the service apart. A failed run exits 1 (an error in
the journal) and is retried by the next trigger or the timer; every run writes `/srv/dl/mxl/.sync/status.json`, which
`publish_release.py list` shows as `Space sync: ok at ...` or `Space sync: FAILED at ...: <error>`. Updating the script:
copy it again, nothing to restart.

### Checks (from the PC)

```powershell
curl.exe -sI https://files.werdegames.com/mxl/channels/stable.json | findstr /i "HTTP cache-control content-type cf-cache-status"
#   200, cache-control: no-cache, content-type: application/json, cf-cache-status: REVALIDATED / EXPIRED / MISS / DYNAMIC (never a stale HIT)
curl.exe -sI https://files.werdegames.com/mxl/objects/ab/<some sha256> | findstr /i "HTTP cache-control cf-cache-status accept-ranges"
#   cache-control: public, max-age=31536000, immutable; cf-cache-status: HIT from the second request on
curl.exe -sI https://files.werdegames.com/mxl/releases/0.70.0/D2R-XL-0.70.0.7z | findstr /i "HTTP content-type cache-control"
#   content-type: application/x-7z-compressed, cache-control: public, max-age=3600
curl.exe -sI https://files.werdegames.com/mxl/.sync/status.json    # 403 or 404: dot files are never uploaded
py -3 tools/publish_release.py verify --url https://files.werdegames.com/mxl/ --channel test   # as for dl.werdegames.com
py -3 tools/publish_release.py list --target $t --identity $k      # last line: Space sync: ok at ...
```

### Stale channels, purging by hand

Cloudflare follows the origin's `Cache-Control` with the cache rule above: objects are cached for a year (they never
change), releases / launcher / assets for an hour, and `channels/*.json` (`no-cache`) are revalidated on every request,
so a new channel version shows at once, and the downloader's sha256 check of the manifest catches anything half-updated.
The Spaces CDN in between has its own edge TTL. If a channel ever shows an old version after `list` says the sync is done
(compare `curl.exe -s https://files.werdegames.com/mxl/channels/stable.json` with the same path on dl.werdegames.com),
purge by hand:
- DigitalOcean: Spaces, `d2r-xl`, Settings, CDN, Purge cache: `mxl/channels/*`.
- Cloudflare: Caching, Configuration, Purge Cache, Custom purge, URLs `https://files.werdegames.com/mxl/channels/stable.json`
  and `https://files.werdegames.com/mxl/channels/test.json`.

A `publish --force` of an existing version rewrites files under `releases/<v>/`: purge `mxl/releases/<v>/*` the same way
(or wait an hour).

### dl.werdegames.com stays

`/srv/dl/mxl` on the server is the origin of everything; Caddy keeps serving it at `https://dl.werdegames.com/mxl/`
(section 4) as the backup and for checks. If the Space or Cloudflare has a problem, a downloader can be pointed back at it
with `--site https://dl.werdegames.com/mxl/`; the Space can always be rebuilt from the server with
`systemctl start mxl-dl-sync` (after a big cleanup: `dl_sync.py --allow-mass-delete` once by hand). The downloader's default
site is `https://files.werdegames.com/mxl/` since downloader 0.2.1 (`launcher/src/win/app.h`, `DefaultSite`; live 2026-10-27 through the Cloudflare Worker below).

### The Cloudflare Worker in front of the Space (what is live)

The plan above (a proxied CNAME to the Spaces CDN with a custom subdomain) fails. DigitalOcean's Spaces CDN runs on Cloudflare itself, so a proxied record pointing at it gets **error 1014 (CNAME Cross-User Banned)**. A grey-cloud record would show players the Cloudflare Origin CA certificate, which only Cloudflare trusts. A proxied CNAME to the plain Space gets NoSuchBucket, and rewriting the Host header needs Enterprise.

What runs instead is the Worker `d2r-xl-files` (free plan), with the custom domain `files.werdegames.com`. It fetches `https://d2r-xl.fra1.digitaloceanspaces.com<path>`, which is not behind Cloudflare:
- It serves only GET and HEAD, and only paths under `/mxl/` without dot segments.
- It forwards `Range`, `If-None-Match` and `If-Modified-Since`.
- It fetches with `cf: { cacheEverything: true }`, so caching follows the origin's Cache-Control: objects are HITs from the second request on, and channels are revalidated (REVALIDATED) every time.
- A 403 from the Space (missing file, listing restricted) becomes a 404.
- It drops the `x-amz-*` headers.

The source is kept in the Cloudflare dashboard (Workers & Pages, `d2r-xl-files`, Edit code). It was checked on 2026-10-27:
- `verify --url https://files.werdegames.com/mxl/` passed;
- Range requests return 206;
- `channels/nope.json`, `.sync/status.json` and `/` return 404.

The DigitalOcean CDN custom subdomain and the Cloudflare cache rule are no longer used, and leaving them on does no harm.

Cloudflare's zone **Browser Cache TTL** (4 h) rewrites the Cache-Control that clients see, e.g. `max-age=14400` on channels. The edge still revalidates channels on every request. The downloader (WinHTTP) keeps no HTTP cache, so this only affects browsers. To pass the origin's headers through unchanged, set Caching, Configuration, Browser Cache TTL to "Respect Existing Headers" (zone-wide).
