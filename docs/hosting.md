# Download host (dl.werdegames.com/mxl)

Where players' launchers get the mod from (owner decision 2026-10-26: our own server, not GitHub; the forge needs a sign-in
for everything). The host is **a folder served by Caddy `file_server`**, plus one small script that runs there when an
upload arrives. We upload over SFTP with `tools/publish_release.py`; players read the folder over HTTPS. What is in the
folder and how the launcher uses it: [release.md](release.md) "Hosting layout".

```
Developer PC --sftp (key, chroot)--> /srv/dl/incoming/<v>/ --dl_ingest.py (systemd, as mxlup)--> /srv/dl/mxl/ <--Caddy--HTTPS-- launcher
```

Why the extra step: the uploader's line is slow (about 160 KB/s up) and each SSH login takes seconds, so the publisher
sends only the objects the host lacks, packed into one 7z, and the server unpacks and checks them, builds the players'
7z itself (355 MB that never cross the slow line) and switches the channel last. (The Caddy proxy in front of the forge
drops long HTTP uploads, which is why we upload over SSH and not HTTP.)

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
the launcher and the channel file (last), then deletes the upload. Anything that fails a check is refused and left in
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

	# Caddy compresses what is worth it; the launcher checks sha256 of the decoded bytes.
	encode zstd gzip {
		match {
			header Content-Type application/json*
			header Content-Type text/*
		}
	}

	# objects/<sha256[0:2]>/<sha256> never change: cache forever
	header /mxl/objects/* Cache-Control "public, max-age=31536000, immutable"
	# the channel files are what the launcher polls: always revalidate
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
- **CORS is not needed**: the launcher is a program, not a web page. No `Access-Control-*` headers.
- Objects have no file extension, so Caddy sniffs their type: text tables come out as `text/*` and are compressed, binaries
  (dds, models, the dll) are not. If `publish_release.py verify` (below) shows `Range requests` failing, or a
  `Content-Encoding` on a 206 answer, drop the `text/*` line from `match`: resumable downloads win over a few MB.
- `Cache-Control` of `releases/`, `launcher/`, `assets/` is deliberately short; only `objects/` is immutable.
- Reload: `caddy validate --config /etc/caddy/Caddyfile && systemctl reload caddy`. Caddy needs read access to `/srv/dl/mxl`
  (0755 / 0644 as above; `ls -l` should show world-readable).
- Serve nothing else from this site name: the last `handle` answers 404 for every other path (also `/incoming/`); the forge
  keeps its own name.
- `header` also stamps error answers, so a request for an object that is not uploaded yet gets `immutable` on its 404. The
  launcher only asks for objects a published manifest names, which are all there, so this only matters for a browser or a CDN.

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
MB packed; a release that changes a few tables: a few MB), the manifest, the notes and the launcher if given. A dropped
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
7. Tell the coordinator the root URL, and whether sshd is on port 22 (the launcher lane takes the root URL as its setting).
