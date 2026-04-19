# OpenParsec

A community-maintained open-source revival of the classic space combat game Parsec.

💬 **[Join us on Discord](https://discord.gg/G7ZejM5N5h)**
▶️ **[Gameplay video (Nov 2021)](https://www.youtube.com/watch?v=BbiaSOe4HbI)**

---

## Table of Contents

- [Playing the game](#playing-the-game)
- [Building from source](#building-from-source)
- [Running the client](#running-the-client)
- [Running a game server](#running-a-game-server)
  - [settings.con reference](#settingscon-reference)
  - [_level.con reference](#_levelcon-reference)
- [Running a master server](#running-a-master-server)
- [Multi-server networks & stargates](#multi-server-networks--stargates)

---

## Playing the game

The easiest way to play is to download the latest Windows installer from the
[GitHub Actions artifacts](../../actions) — pick the most recent successful
**Build on windows** run and download `openparsec-installer.exe`. Run the
installer, launch **Open Parsec** from the Start menu, and connect to a server
from the in-game server browser.

---

## Building from source

You will need a copy of the game assets submodule:
```
git clone --recurse-submodules https://github.com/CrazySpence/openparsec.git
```

### Windows

- Visual Studio 2019 or later (2022 recommended)
- Open `platforms\vs\Parsec.sln`
- Select the **Parsec** target (client) or **parsec_server** target
- Set configuration to **Release / x64** and build

SDL2 and SDL2\_mixer DLLs are included under `platforms\vs\lib\x64\` — no
separate download needed.

The Windows CI workflow (`windows-build.yml`) produces a self-contained
installer via NSIS that bundles all required DLLs including the MSVC runtime.

### Linux

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt install libsdl2-dev libsdl2-mixer-dev premake4

cd platforms/premake
premake4 gmake

make client    # builds parsec_root/client/parsec
make server    # builds parsec_root/server/parsec_server
make           # builds both
```

### macOS

- Xcode 9.3 or later
- Open `platforms/xcode/Parsec.xcodeproj`
- Select target **parsec** (client) or **parsec\_server**
- Build → binaries land in `parsec_root/{client,server}/`

### Raspberry Pi

Use `platforms/premake-rpi/` instead of `platforms/premake/`. You will need
the Broadcom GLES packages in addition to SDL2 and SDL2\_mixer.

### ChromeOS

Enable the Linux environment and follow the Linux instructions above.

---

## Running the client

After building, the client binary and all required files are in
`parsec_root/client/`. You also need the assets from the
[openparsec-assets](https://github.com/OpenParsec/openparsec-assets)
repository in the same directory (the submodule populates this automatically).

Launch the binary. Use the in-game server browser to find public servers, or
enter a server address directly.

---

## Running a game server

After building, the server binary and all required files are in
`parsec_root/server/`.

```bash
cd parsec_root/server
./parsec_server          # Linux / macOS
parsec_server.exe        # Windows
```

The server reads `boot_sv.con` on startup, which loads ship and object
definitions and then executes `settings.con` and `_level.con`. Edit those two
files to configure your server.

### settings.con reference

| Command | Example | Description |
|---------|---------|-------------|
| `sv.conf name <name>` | `sv.conf name Enyo` | Server name shown in the browser |
| `sv.conf maxplayers <n>` | `sv.conf maxplayers 16` | Maximum simultaneous players |
| `sv.conf simfreq <hz>` | `sv.conf simfreq 100` | Physics simulation rate (default 100) |
| `sv.serverid <n>` | `sv.serverid 1` | Unique numeric ID for this server. Used by stargates to identify link targets |
| `sv.netconf.port <port>` | `sv.netconf.port 7777` | UDP port to listen on (default 7777) |
| `nebula.id <n>` | `nebula.id 3` | Background nebula / skybox (1–6, see table below) |
| `sv.game.killlimit <n>` | `sv.game.killlimit 15` | Kills required to end a round |
| `sv.game.timelimit <sec>` | `sv.game.timelimit 600` | Round time limit in seconds |
| `sv.game.restart.timeout <sec>` | `sv.game.restart.timeout 10` | Seconds between round end and restart |
| `sv.game.extras.autocreate <0/1>` | `sv.game.extras.autocreate 1` | Automatically spawn powerup pickups |
| `sv.game.extras.max <n>` | `sv.game.extras.max 25` | Maximum pickups in the world at once |
| `sv.masterserver.conf name <host> interval <sec>` | `sv.masterserver.conf name master.openparsec.com interval 5` | Register with a master server |
| `sv.masterserver.sendheartbeat <0/1>` | `sv.masterserver.sendheartbeat 1` | Enable heartbeat to master server |

**Nebula IDs:**

| ID | Appearance |
|----|------------|
| 1 | Purple |
| 2 | Green |
| 3 | Red |
| 4 | Grey |
| 5 | Blue |
| 6 | Orange |

### _level.con reference

`_level.con` places the objects that live in the game world: stargates (links
to other servers), teleporters (local warp pads), and planets.

#### Stargates — `sv.link`

Links your server to another server. Players fly into the stargate to transit
to the linked server. The loadout (ammo, weapons, shields) is preserved across
the jump via the master server.

```
sv.link serverid <id> pos ( <x> <y> <z> ) dir ( <dx> <dy> <dz> )
```

| Parameter | Description |
|-----------|-------------|
| `serverid` | The `sv.serverid` value of the destination server |
| `pos` | World-space position of the gate |
| `dir` | Direction vector the gate faces (usually `0 0 1`) |

Example — two gates side by side:
```
sv.link serverid 2  pos ( 500   0  1000 ) dir ( 0 0 1 )
sv.link serverid 4  pos ( -500  0  1000 ) dir ( 0 0 1 )
```

#### Teleporters — `tp.create`

Local warp pads: a player enters the entrance and exits at the exit point.
Unlike stargates, teleporters keep you on the same server.

```
tp.create pos ( <x> <y> <z> ) expos ( <ex> <ey> <ez> ) exit_rot_phi <phi> exit_rot_theta <theta>
```

| Parameter | Description |
|-----------|-------------|
| `pos` | Entrance position |
| `expos` | Exit position |
| `exit_rot_phi` | Exit orientation — pitch (degrees) |
| `exit_rot_theta` | Exit orientation — yaw (degrees) |

Example:
```
tp.create pos ( 500 500 500 ) expos ( 200 2000 2000 ) exit_rot_phi 50 exit_rot_theta 180
```

#### Planets — `sv.planet`

Decorative (and collidable) planets.

```
sv.planet pos ( <x> <y> <z> ) rotspeed <n> ring <0/1>
```

| Parameter | Description |
|-----------|-------------|
| `pos` | Centre position in world space |
| `rotspeed` | Rotation speed (arbitrary units; 8–16 is typical) |
| `ring` | `1` to add a Saturn-style ring, `0` for none |

Example:
```
sv.planet pos ( 0 0 2000 )    rotspeed 16 ring 1
sv.planet pos ( -3000 500 1000 ) rotspeed 8  ring 0
```

---

## Running a master server

The master server tracks which game servers are online and provides the list to
clients. It also acts as the transit registry — it temporarily holds a player's
loadout when they jump through a stargate so the destination server can restore
it on join.

The master server is the same binary as the game server, started with the
`--master` flag:

```bash
./parsec_server --master
```

Game servers register by adding to their `settings.con`:
```
sv.masterserver.conf  name <master-ip-or-hostname>  interval 5
sv.masterserver.sendheartbeat  1
```

The default master server is `master.openparsec.com`.

---

## Multi-server networks & stargates

You can link multiple game servers into a network that players can fly between
via stargates.

**Setup checklist:**

1. Each server must have a unique `sv.serverid` in its `settings.con`.
2. Every server must register with the same master server.
3. In each server's `_level.con`, add an `sv.link` for each neighbouring
   server, using the neighbour's `sv.serverid` and its public IP/port.
4. Stargates are bidirectional by convention but must be placed on **both**
   servers — a gate on server A pointing to B, and a gate on server B pointing
   back to A.

**Loadout transit:**

When a player flies through a stargate the client saves their current ammo,
weapons, energy and shields to the master server. The destination server
queries the master on join and restores the loadout. Records expire after
5 minutes if the player never connects to the destination.

**Example three-server layout:**

```
settings.con (Enyo,     serverid 1, nebula 5)    → sv.link serverid 2
settings.con (McAulliffe, serverid 2, nebula 2)  → sv.link serverid 1, sv.link serverid 3
settings.con (Gimle,    serverid 3, nebula 3)    → sv.link serverid 2
```
