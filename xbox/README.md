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

The HUD shows frags, time remaining, health, armour, and ammunition in the magazine and reserve. Walk over the map's ammo, health, helmet, and armour pickups to collect them. The bot can collect them too.

## Build and run

From the repository root, install the Python asset-baking dependencies and bake the map, character poses, textures, pickups, and sounds from the bundled game data.

```sh
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
| `platform_nxdk.c` | Xbox entry point, controller input, SDL audio mixing, pbkit display, and reboot |
| `match.cpp` | Portable local duel, bot decisions, active weapons, item timers, respawns, and intermission |
| `navigation.c` | Map collision, line of sight, and shortest walkable paths around obstacles |
| `render.c` | Textured scene, animated bot sprites, pickups, visibility at the crosshair, and weapon overlay |
| `bake.py`, `assets.s` | Import and embed original game assets including free-for-all player starts |

The duel rules and bot navigation are local to this port. The renderer approximates slopes and diagonal walls with flat grid cells. The complete PC bot AI, recoil and spread simulation, jump physics, placed map models, other weapons, and multiplayer remain future work.

## Verification

With baked assets and host C and C++ compilers on PATH, run `python xbox/tests/run.py`. Set `CC` and `CXX` to select the compilers. The regression uses the actual map and center-pixel targeting for the opening volley, then checks bot pursuit, frags, respawns, the score and time limits, and rematch reset.

On November 24, 2026, XBE SHA-256 prefix `583a61ed` booted on physical Xbox after FTP readback. Player-one KVM input scored the opening frag, reloaded the rifle from 10/40 to 20/30, switched to the pistol, and fired it. The bot respawned, pursued and killed the player, and won 3–1. Start reset the score to 0–0, health to 100, and the clock to 1:30. Both sticks changed the player view in the rematch. The short input sequence in `bot-match.json` exercises the opening burst, reload, and weapon switch using the ESP-KVM scenario runner. Use a live video stream to review its screenshots.
