# Download host (dl.werdegames.com/mxl)

Where players' launchers get the mod from (owner decision 2026-10-26: our own server, not GitHub; the forge needs a sign-in
for everything). The host is **a folder served by Caddy `file_server`**; nothing runs on it. We write to the folder over
SFTP with `tools/publish_release.py`; players read it over HTTPS. What is in the folder and how the launcher uses it:
[release.md](release.md) "Hosting layout".

```
Developer PC --sftp (SSH, key, chroot, write only here)--> /srv/dl/mxl/ <--file_server-- Caddy <--HTTPS-- launcher
```

(The Caddy proxy in front of the forge drops long HTTP uploads, which is why we upload over SSH and not HTTP.)

Everything below is for the server's owner. `HOST` is the server (the one behind git.werdegames.com). Names used: user
`mxlup`, folder `/srv/dl/mxl`, URL `https://dl.werdegames.com/mxl/`; change them as you like, keep the three consistent.

## 1. DNS

Create `dl.werdegames.com` (A and AAAA, the same addresses as the forge) at the DNS provider. Caddy gets the certificate
by itself once the name resolves and ports 80 and 443 reach Caddy.

## 2. Folder, upload user, permissions

```
/srv/dl                    root:root   0755   the SFTP chroot; must belong to root and not be writable by anyone else
/srv/dl/mxl                mxlup:mxlup 0755   the only writable place; the site root
  channels/ releases/ objects/ launcher/ assets/     created by publish_release.py (files 0644, folders 0755)
```

As root on the server (Debian/Ubuntu names; adapt for others):

```sh
useradd --no-create-home --shell /usr/sbin/nologin mxlup
usermod -p '*' mxlup              # no password at all, but not "locked" (a locked account is refused even with a key)
install -d -m 0755 -o root  -g root  /srv/dl
install -d -m 0755 -o mxlup -g mxlup /srv/dl/mxl
install -d -m 0755 /etc/ssh/authorized_keys.d
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
so Caddy (another user) can read them. Then:

```sh
sshd -t && systemctl reload ssh        # the unit is called sshd on some systems
```

If the forge's own SSH server already uses port 22, either the system sshd listens on another port (put `:port` in the
target: `sftp://mxlup@HOST:2222/mxl`) or this user is served by a second sshd instance. Check with `ss -ltnp | grep ssh`.

Check from the publishing PC (accept the host key on the first connection, so later batch runs do not stop at the question):

```powershell
sftp -i $HOME\.ssh\mxl_publish mxlup@HOST         # prompt "sftp>": `ls` shows mxl, `cd /` stays inside the chroot
ssh  -i $HOME\.ssh\mxl_publish mxlup@HOST         # must be refused ("This service allows sftp connections only.")
```

**rrsync instead of an SFTP chroot** (`command="rrsync -wo /srv/dl/mxl",restrict ssh-ed25519 ...` in `authorized_keys`) also
restricts a key to one folder, but it is for `rsync` clients. `publish_release.py` speaks SFTP (and a plain folder), not rsync,
so use the chroot above. Running the publisher **on the server** itself is also possible: `--target dir:/srv/dl/mxl`.

## 3. Caddy

In the server's Caddyfile (Caddy 2). The host serves `/srv/dl`, and only the `/mxl/` part of it:

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
		# never served: half-written uploads (*.part-<hex>) and dot files
		@hidden path_regexp hidden (?:/\.|\.part-[0-9a-f]+$)
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
- Serve nothing else from this site name: the last `handle` answers 404 for every other path; the forge keeps its own name.
- `header` also stamps error answers, so a request for an object that is not uploaded yet gets `immutable` on its 404. The
  launcher only asks for objects a published manifest names, which are all there, so this only matters for a browser or a CDN.

## 4. First publish, checks

On the PC with the finished `dist/` from `tools/package_release.py` (docs/release.md "Making a release"):

```powershell
$t = "sftp://mxlup@HOST/mxl"       # add :port when sshd is not on 22
py -3 tools/publish_release.py list --target $t --identity $HOME\.ssh\mxl_publish           # empty host: both channels "(none)"
py -3 tools/publish_release.py publish --version 0.70.0 --channel test --target $t --identity $HOME\.ssh\mxl_publish
py -3 tools/publish_release.py verify  --url https://dl.werdegames.com/mxl/ --channel test   # fetches every object over HTTPS, sha256
curl.exe -sI https://dl.werdegames.com/mxl/channels/test.json                                # Cache-Control: no-cache
curl.exe -sI https://dl.werdegames.com/mxl/objects/ab/<some sha256>                          # ... immutable, Accept-Ranges: bytes
py -3 tools/publish_release.py promote 0.70.0 --channel stable --target $t --identity $HOME\.ssh\mxl_publish
```

`MXL_PUBLISH_TARGET` in the environment replaces `--target`. The first publish sends the whole mod (about 1.2 GB of objects at a
full build, plus the zip, about 0.5 GB, and the 7z, about 0.36 GB); later releases send only objects the host does not have
(a release that changes a few tables sends a few MB, plus the zip and the 7z, which are whole files every time). A publish
that dies half-way shows nothing to players and is resumed by running the same command again.

Maintenance:
- `list` shows channels and releases. `gc --keep 3 --dry-run`, then `gc --keep 3`: removes the older releases (their zip and 7z too)
  and the objects nothing else uses; releases a channel points to are never removed. Do not run `gc` while a publish runs
  (it would delete the objects the publish just uploaded and no manifest names yet).
- Disk: about one full zip (0.5 GB) and one 7z (0.36 GB) per kept release plus the objects of all kept releases (shared).
- Backup: the folder is rebuildable from `dist/` of each release (zip, 7z, manifest); keep those.
- Rolling back: `promote <older version> --channel stable` (the older release must still be on the host).
- Offline test of the same layout, no server: `py -3 tools/serve_dir.py <folder>` serves a `dir:` target with Range support and the
  same `Cache-Control` rules (`publish --target dir:<folder>`, then `verify --url http://127.0.0.1:<port>/`).

## 5. What the owner has to do, in order

1. DNS: `dl.werdegames.com` -> the server.
2. On the server: the commands of section 2 (user, folders, key line, `sshd_config` block, `sshd -t && reload`).
3. On the server: the Caddyfile block of section 3, `caddy validate`, reload.
4. On the PC: generate the key, put `mxl_publish.pub` on the server (step 2), `sftp`/`ssh` checks of section 2 (accepts the host key).
5. Run section 4 once with a `--no-data` package or the first real release on `test`; `verify` must end with
   `N of N objects verified` and `Range requests: ok (206)`.
6. Tell the coordinator the root URL, and whether sshd is on port 22 (the launcher lane takes the root URL as its setting).
