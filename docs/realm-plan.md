# Private realm plan (Phase 7, preliminary)

Status: draft (2026-10-20). Starts after single-player Median XL works on D2R (Phases 2-6). Revisit and edit
before starting; nothing here is built yet.

"Private realm" here means characters stored on our own server, with games hosted through D2RLoader's TCP/IP.
That is the model D2R Reimagined runs (ladder mode) and Project Lage is building. It does not mean a Battle.net
emulator or a classic D2GS behind a proxy (Blizzless): that would need Median's game logic written a second time
for the classic server, and it is in alpha with Act 1 only.

## How Reimagined does it (reference)

From reading their launcher (github.com/D2R-Reimagined/reimagined-launcher, commit fd5f721, 2026-10-15). Their
in-game plugins and API server are closed source; plugin behaviour below is from their README.

- **API** at api.d2r-reimagined.com: accounts, ladder schedule, signed packages, launch tickets, saves, lobby
  regions/servers, chat, hardcore deaths, trades.
- **Auth:** browser PKCE login to the website, refresh token stored encrypted on the PC. Each launch mints a
  short-lived game token, revoked when the game exits.
- **Credentials file:** the token goes to `<game>/reimagined-secrets/session.toml`, outside `mods/`, because
  D2RLoader can copy a host's mod folder to joining players. Plugin configs only carry the API URL and ladder id.
- **Ladder package:** a zip of the whole mod with a manifest signed with ECDSA P-256. The launcher checks the archive
  hash, the signature and every file hash, and rejects undeclared files. The package pins the D2RLoader build,
  the D2RCore.dll hash and the game version. Plugins not on the approved list are moved aside.
- **Separate saves:** the launcher rewrites `modinfo.json` `savepath` to a per-ladder folder.
- **Save sync** (`d2rl-server-saves.dll`, in game):
  - On load it pulls the server's character list and makes the folder match. The server wins.
  - It uploads each save during play, plus a final push on exit.
  - Saves it replaces locally go to `.server-saves/backups/`. Saves the server rejects go to
    `.server-saves/pending-upload/` and are offered again next launch.
  - It writes `.server-saves/status.json` (state, plus each confirmed save's version and SHA-256), which the
    launcher reads after exit.
  - API unreachable: the folder is emptied, so no characters.
- **Hosting:** players host through D2RLoader TCP/IP. The lobby API also lists per-region game servers (games,
  players, max games), so ladder games may also run on their own hosts.
- **Anti-cheat:** everything is enforced on the player's PC (signatures, hashes, allowlist, pins). Nothing shows
  the server checking save contents.

## Our design (minimum version)

### 7a. Save sync plugin (`mxl-realm`, D2RLoader plugin)
- Realm characters live in their own save folder, set through `modinfo.json` `savepath`, apart from offline
  characters.
- On startup, before character select:
  1. Fetch the character list (name, revision, SHA-256).
  2. Download whatever is newer on the server.
  3. Keep displaced local files in `.realm/backups/`.
- During play, upload a save after each write. On exit, do a final upload and wait for confirmation.
- Upload as a set: `.d2s`, its `.d2rl` sidecar (body hash and save hash must match, see
  [save-format.md](save-format.md)), and the shared stash (`.d2i`; Median's 14 x 14 shared pages).
- Rejected or failed uploads: keep them in `.realm/pending/` and retry next launch. Server unreachable: show no
  realm characters (same as Reimagined).
- Read the token from a file outside `mods/MedianXL`, never from the plugin's own config.
- Open: hook the game's own save writer, or watch the folder for changes. Hooking is more reliable (we know when a
  save is complete) but needs the address in 3.3.93847.

### 7b. Realm API (small web service on a VPS)
- **Accounts:** register/login, issue a long-lived refresh token and short-lived game tokens. A private test can
  start with invite codes.
- **Saves:**
  - `GET /characters` lists the account's characters with revision and SHA-256.
  - `GET /characters/{name}` downloads a character.
  - `PUT /characters/{name}` uploads one, with the expected previous revision so stale uploads are rejected.
  - `DELETE /characters/{name}` deletes one.
- Keep the last N revisions of every save (Project Lage keeps 20) so mistakes can be rolled back.
- **Basic checks on upload:** header signature, version 0x69, size and checksum, `.d2rl` hashes, file size limits,
  one character name per account.
- Later: parse the save with tools/save to reject impossible stats or items.
- Stack: open (anything small: Python FastAPI or .NET minimal API, SQLite or Postgres, saves on disk or object
  storage).

### 7c. Launching (minimum version: no launcher)
- A small script writes the token file and starts
  `D2RLoader.exe -mod MedianXL -txt` with the realm savepath.
- Later, a launcher (like Reimagined's) that signs in through the browser, installs and checks a signed mod
  package, and pins the D2RLoader and game builds.
- Every client must run the same mod build. Use a build hash in the API handshake to refuse mismatched clients.

### 7d. Hosting games
- Start with player-hosted D2RLoader TCP/IP games, which Phase 6 already has to make work.
- Later, optionally: one always-on host on the VPS running D2R with our plugins, plus a game list in the API.
  Needs a way to run D2R unattended (auto-create games, restart on crash).

### 7e. Later features (only if wanted)
Ladder seasons and a fresh-start save folder per season, hardcore death feed, global chat, trade listings,
signed packages with a plugin allowlist, server-side save validation.

## Requirements on the port (keep these true now)
- Median logic stays on the game-server side of the plugins; the client side only draws UI. TCP/IP and a realm both
  depend on it.
- Median's state must live in the save files (`.d2s`, `.d2rl`, `.d2i`), not in plugin-side files, or it will not
  reach the realm.
- Multiplayer packet limits, such as the state-id width in [plugin-spec.md](plugin-spec.md), get checked during
  Phase 6 TCP/IP testing.

## Open questions
- Which D2R save writer to hook for 7a, and whether D2RLoader's SDK already offers a "save written" callback.
- Shared stash: one `.d2i` per account per realm. Check how D2R names it per mod/savepath.
- How D2RLoader TCP/IP handles a joining player's characters (they stay client-side; a host cannot vouch for them).
- When to wipe (seasons) and whether to keep an offline copy for players.

## Steps (rough order)
1. Phase 6 done: TCP/IP games with two clients, saves reload correctly.
2. Realm API: accounts and save storage (7b), run locally.
3. `mxl-realm` plugin: download on start, upload on save and exit (7a).
4. Launch script (7c), test with two accounts on two PCs.
5. Deploy the API to a VPS, invite testers.
6. Decide on 7d / 7e from what testers need.
