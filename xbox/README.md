# AssaultCube on original Xbox

Play a local one-on-one bot deathmatch on the original `ac_desert` by makkE and Arghvark. Press Start to begin. Reach three frags to win, or lead when the 90-second clock expires. A tied score at time produces a draw. Both fighters respawn after two seconds.

| Control | Action |
| --- | --- |
| Left stick | Move and strafe |
| Right stick | Turn and aim vertically |
| Right trigger | Fire. Hold for automatic rifle fire, tap for each pistol shot |
| X | Reload from reserve ammunition |
| Y | Switch between the MTP-57 rifle and MK-77 pistol |
| Start | Begin the match or play again from the result screen |
| LT + RT + Back + Black | Hold for one second to reboot |

Controllers can be plugged in or replaced while the game is running. Any of the four ports can control the duel. The title and result screens show the number of connected controllers.

The HUD shows frags, time remaining, health, armour, and ammunition in the magazine and reserve. Walk over the map's ammo, health, helmet, and armour pickups to collect them. The bot can collect them too.

The Xbox selects 1280x720 at 60 Hz when enabled in the console video settings and supported by the cable. It falls back to 640x480. The map uses batched geometry, filtered mipmapped textures, and lighting baked from the map entities. The player and first-person weapons use the bundled MD2 and MD3 meshes. The HUD updates its frame counter every two seconds.

## Build and run

From the repository root, install the Python asset-baking dependencies and bake the map, character poses, textures, pickups, and sounds from the bundled game data.

```sh
git submodule update --init xbox/third_party/pbgl
python -m pip install Pillow numpy soundfile
python xbox/bake.py
```

In an activated nxdk shell with SDL2, pbkit, C++ support and the XISO tools, run the following from the repository root.

```sh
make -C xbox -j4
```

Copy `xbox/bin/default.xbe` into its own Games folder on a homebrew-capable original Xbox. The executable includes the baked assets. The build also produces `xbox/assaultcube.iso`. Original asset credits and licenses remain in the map and packages directories.

## Shared gameplay and platform code

The PC client, dedicated server, and Xbox match consume the same `playerstate` class and weapon and item definitions in `source/src/playerstate.h` and `source/src/weaponstats.inc`. The Xbox runs the original free-for-all spawn loadout, gun statistics, pickup limits, and armour damage calculation for both fighters.

| Xbox file | Responsibility |
| --- | --- |
| `platform_nxdk.c` | Xbox entry point, controller input, SDL audio mixing, video mode selection, and reboot |
| `controllers.c` | Track attached SDL controller instances across USB disconnects and device-index changes |
| `match.cpp` | Portable local duel, bot decisions, active weapons, item timers, respawns, and intermission |
| `navigation.c` | Map collision, line of sight, and shortest walkable paths around obstacles |
| `gpu_render.c` | NV2A world and model drawing, skybox, HUD, and vblank presentation through pbGL and pbkit |
| `bake.py`, `bake_gpu.py`, `assets.s` | Import and embed original game assets including free-for-all player starts |

The duel rules and bot navigation are local to this port. The renderer approximates slopes and diagonal walls with flat grid cells. The complete PC bot AI, recoil and spread simulation, jump physics, placed map models, other weapons, and multiplayer remain future work.

## Verification

With baked assets and host C and C++ compilers on PATH, run `python xbox/tests/run.py`. Set `CC` and `CXX` to select the compilers. The regression uses the actual map and crosshair ray and player-cylinder intersection for the opening volley, then checks bot pursuit, frags, respawns, the score and time limits, and rematch reset.

For the input regression, compile `controllers.c` and `tests/controllers.c` together with desktop SDL2 (2.0.14 or newer) and run the resulting executable with two free controller slots. It replaces a virtual gamepad between frames, discards queued device events, and checks Start on the replacement while the other pad stays open.

On November 24, 2026, XBE SHA-256 prefix `583a61ed` booted on physical Xbox after FTP readback. Player-one KVM input scored the opening frag, reloaded the rifle from 10/40 to 20/30, switched to the pistol, and fired it. The bot respawned, pursued and killed the player, and won 3–1. Start reset the score to 0–0, health to 100, and the clock to 1:30. Both sticks changed the player view in the rematch. The short input sequence in `bot-match.json` exercises the opening burst, reload, and weapon switch using the ESP-KVM scenario runner. Use a live video stream to review its screenshots.

On November 24, 2026, build `aa84a666` ran on the physical Xbox at native 1280x720. The earlier CPU build (`1fa49730`) rendered at 320x240. The KVM replay used the opening rifle burst, reload, pistol switch, return fire, bot pursuit, and respawns.

| Physical Xbox sample | CPU build | NV2A build |
| --- | ---: | ---: |
| Opening courtyard | 32.1 FPS | 60.0 FPS |
| Nine combat and result captures | 32.1–35.1 FPS | 59.5–60.0 FPS |
| Separate rematch, after moving and turning | — | 59.0 FPS |

Each reading counts game frames completed over approximately two seconds. The samples do not measure unique scanouts or worst-case frame time. The HD replay finished 3–1 to the bot, and Start began a fresh match.

xemu 0.8.139 booted the same XISO with HD enabled and with the 4:3 fallback. The emulated controller started the duel, scored a rifle frag, reloaded, switched to the pistol, fired, and turned down a corridor.
