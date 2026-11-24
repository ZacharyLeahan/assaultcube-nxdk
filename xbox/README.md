# AssaultCube on original Xbox

This first nxdk build boots straight into a single-player target range on the bundled `ac_desert` map by makkE and Arghvark. Each target takes five center-mass hits. The next target appears nearby, and the score carries over until you start a new range.

| Control | Action |
| --- | --- |
| Left stick | Move and strafe |
| Right stick | Turn and look up or down |
| Right trigger | Fire the MTP-57, hold for automatic fire |
| X | Reload the 20-round magazine |
| Start | Move to the next map spawn and reset the score |
| LT + RT + Back + Black | Hold for one second to reboot |

## Build and run

From the repository root, use Python 3 with Pillow, NumPy and SoundFile installed. The asset baker reads the original map, MD2 meshes, skins, textures, and Ogg sounds from `packages/`. Generated data goes in `xbox/generated/`. Rerun the baker after changing those inputs.

```sh
python -m pip install Pillow numpy soundfile
python xbox/bake.py
```

In an activated nxdk shell, run the following from the repository root. The Makefile also accepts `NXDK_DIR` explicitly. The SDK needs its SDL2 and pbkit libraries and its XISO tools.

```sh
make -C xbox -j4
```

Copy `xbox/bin/default.xbe` into its own Games folder on a homebrew-capable original Xbox, or load `xbox/assaultcube.iso` as a disc in xemu. The executable includes the baked assets. Original asset credits and licenses remain in the map and packages directories.

## Implementation scope

The Xbox-specific renderer follows grid boundaries through the CGZ map and draws textured floors, ceilings, and wall spans at 320×240, doubled to 640×480. Player starts, wall and floor heights and texture slots come from the map. Corner cubes use solid blocks and heightfields use flat cells. Placed map models, lighting and clip entities still need porting. The player and rifle use static views baked from the original meshes.

The MTP-57 uses the PC game's 20-round magazine, 120 ms shot interval, 22 damage and two-second reload. The target range has its own input, collision, scoring and hitscan logic. The original client simulation, moving bots, multiplayer, menus, and additional maps remain future port work.

## Source layout

The port builds independently under `xbox/`. The desktop client, its build system and the original asset tree require no changes.

| File | Responsibility |
| --- | --- |
| `platform_nxdk.c` | Xbox entry point, SDL controller and audio devices, pbkit display and reboot |
| `range.c`, `range.h` | Portable C movement, collision, target placement, weapon and score state |
| `render.c`, `render.h` | Portable C scene rendering, target visibility and weapon overlay into a caller-owned framebuffer |
| `bake.py`, `assets.s` | Host-side import of bundled game assets and link-time embedding |

The platform translates controller state into a `RangeInput` and plays the sound event returned by the range. Both range and renderer can be compiled without SDL or nxdk. This keeps Xbox-specific changes local as upstream engine code is brought across.

## Hardware check

On November 24, 2026, both the initial build `b4a72471` and the platform-separated build `3b1fcf72` booted on an original Xbox after exact FTP readback. ESP-KVM player-one input advanced the range score, dropped the magazine from 20 to 15, restored it to 20 with X, moved forward, and turned the camera. The smoke scenario in `range.json` exercises firing and reloading from a fresh launch. Use a live MJPEG stream when inspecting the KVM stills. Audio playback and sustained frame rate have not been measured.
