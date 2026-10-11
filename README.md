# AION2 local server example

> **Educational purposes only.** This repository is a study of the AION2 client's data formats
> and network protocol. It contains **no game client, no game files and no extracted game data**.
> Everything the tools produce comes from a copy of AION2 that you install and own. It is not
> affiliated with or endorsed by NCSOFT; AION2 and its assets belong to their owners. Do not use
> it to connect to, disrupt or impersonate official services, and respect the game's terms of
> service.

A local AION2 lobby/gameplay server (`Sema Local`) and a traffic redirector (`Sema.Redirector`)
that sends the client's lobby connection to it. C++20, Visual Studio 2022 or newer, x64.

## Client version

Verified against AION 2, global release (Steam app 3393110), Steam build 25800038 (updated
2026-10-09), gameplay protocol build `5301` (sent in the gameplay login).

## Build

Open `Sema_Local.sln` and build **Release | x64**, or:

```powershell
msbuild Sema_Local.sln /p:Configuration=Release /p:Platform=x64
.\bin\x64\Release\Sema.Tests.exe     # protocol tests
```

| Project | Output |
| --- | --- |
| `src/protocol` | `Sema.Protocol.lib`: framing, handshake, lobby and gameplay messages |
| `src/data` | `Sema.GameData.lib`: game data from MySQL/MariaDB plus `config/game-data` |
| `src/server` | `Sema Local.exe`: gateway (lobby, port 13700) and gameplay server (port 13328) |
| `src/redirector` | `Sema.Redirector.exe`: WinDivert-based lobby redirector |
| `tests/native` | `Sema.Tests.exe` |

`third_party/WinDivert` holds the WinDivert 2.2.2 x64 binaries and headers (see its `LICENSE`);
the build copies `WinDivert.dll` and `WinDivert64.sys` next to the redirector.

## Run

1. Build the game database with
   [Aion2_Client_Decoding](https://github.com/SemaAlter/Aion2_Client_Decoding).
2. Copy `config/database.example.ini` to `config/database.ini` with the same connection and
   database name, and put `libmysql.dll` (from your MySQL 8 installation) next to
   `bin/x64/Release/Sema Local.exe`.
3. Start one server entry:

   ```bat
   start-elyos-server.cmd              rem or start-asmodians-server.cmd
   start-elyos-server.cmd 192.168.1.10 rem advertise a LAN address to other PCs
   ```

4. On the PC running the client, start the redirector (it asks for Administrator):

   ```bat
   start-redirector.cmd                rem server on this PC
   start-redirector.cmd 192.168.1.10   rem server on another PC
   ```

5. Start the game and choose the **EU** region. The server appears in the server list; with
   `server_id=1312` the client shows that ID's built-in name.

Logs are written to `logs/`.

## Configuration

| File | Purpose |
| --- | --- |
| `config/sema-local-elyos.ini`, `config/sema-local-asmodians.ini` | Server list entry: `server_id`, name, faction, badges, load |
| `config/regions.ini` | Redirector routes: client region key, official lobby endpoint, local target |
| `config/database.ini` | Database connection (from `database.example.ini`) |
| `config/tutorial-skills.csv` | Skill packet sequencing for the tutorial character |
| `config/game-data/*.csv` | Server rows applied over the database at startup (monster stats, tutorial spawns, skills) |

`Sema Local.exe --help` lists every option.

## Recorded phases

Parts of the Elyos tutorial (the world bootstrap, the cutscene handoff, the room objects, the
exit) and character creation are replayed from recordings of the official server. No
recordings are included. Without them the server handles the lobby, login and the protocol
described in `src/`, but the tutorial does not start. Place your own recordings in `scenarios/`
(`tutorial-bootstrap.json`, `tutorial-cutscene-handoff.json`, `tutorial-room-spawns.json`,
`tutorial-exit.json`, `tutorial-login-character.json`, `character-create.json`,
`world-enter.json`, `world-ready.json`; format: `src/server/replay_scenario.cpp`). The launch
scripts pass them when present. `scenarios/` is git-ignored.
