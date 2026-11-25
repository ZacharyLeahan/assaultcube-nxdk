# AssaultCube on original Xbox

Play a local one-on-one bot deathmatch on the original `ac_desert` by makkE and Arghvark. Press Start to begin. Reach three frags to win, or lead when the 90-second clock expires. A tied score at time produces a draw. You respawn after two seconds and the bot returns after five. When you are fragged, the camera pulls back to show your fall. The right stick lets you look around until first-person play resumes.

The easy bot backs away when you close inside its preferred rifle range. It looks for a clear step along a wall and holds position when cornered.

| Control | Action |
| --- | --- |
| Left stick | Move and strafe |
| Left stick forward / back on the well ladder | Climb / descend. Center the stick to hold position |
| Right stick | Turn and aim vertically |
| A | Jump |
| B | Hold to crouch |
| Back | Hold for the overhead map |
| Right trigger | Hold for automatic rifle fire. Tap for each pistol shot or knife swing |
| X | Reload from reserve ammunition. An empty magazine reloads automatically when reserves are available |
| Y | Switch between the MTP-57 rifle and MK-77 pistol |
| D-pad down | Draw the knife or return to the previous firearm. A knife frag scores two points and scatters the original six fragments |
| Start | Begin the match or play again from the result screen |
| LT + RT + Back + Black | Hold for one second to reboot |

Controllers can be plugged in or replaced while the game is running. Any of the four ports can control the duel. The title and result screens show the number of connected controllers.

The HUD shows frags, time remaining, health, and armour. The large ammo count is the magazine and the smaller count is the reserve, as on PC. The crosshair disappears during a reload. Walk over the map's ammo, health, helmet, and armour pickups to collect them. The bot can collect them too.

The map supplies six ambient sound sources. The courtyard has stereo wind and a distant hawk, water becomes audible near the well, and three air conditioners can be located by ear.

Local rifle and pistol impacts use the crosshair depth of the map and its props. A round that hits a barrel throws sparks at the barrel. Bullet holes appear on nearby level surfaces.

The bot uses the original player model scale and animated head position for hit detection. The rifle and pistol deal their normal damage to both the head and torso.

The Xbox selects 1280x720 at 60 Hz when enabled in the console video settings and supported by the cable. It falls back to 640x480. Gunfire briefly brightens the baked light grid around either fighter. Walls, floors, props, pickups and both fighters' models use that lighting. The bot and its rifle cast a moving stencil shadow from their animated meshes. The HUD updates its frame counter every two seconds.

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

Copy `xbox/bin/default.xbe` into its own Games folder on a homebrew-capable original Xbox. With XBMC's FTP server, you can launch it directly by sending `SITE RunXBE F:\Games\AssaultCube\default.xbe` using the installed path. The executable includes the baked assets. The build also produces `xbox/assaultcube.iso`. Original asset credits and licenses remain in the map and packages directories.

## Shared gameplay and platform code

The PC client, dedicated server, and Xbox match consume the same `playerstate` class and weapon and item definitions in `source/src/playerstate.h` and `source/src/weaponstats.inc`. The Xbox runs the original free-for-all spawn loadout, gun statistics, pickup limits, and armour damage calculation for both fighters.

| Xbox file | Responsibility |
| --- | --- |
| `platform_nxdk.c` | Xbox entry point, controller input, SDL audio mixing, video mode selection, and reboot |
| `ambience.c` | Loop the map's ambient sounds and apply each source's volume and hearing radius |
| `controllers.c` | Track attached SDL controller instances across USB disconnects and device-index changes |
| `match.cpp` | Portable local duel, bot decisions, active weapons, item timers, respawns, and intermission |
| `navigation.c` | Map collision, line of sight, and shortest walkable paths around obstacles |
| `gpu_render.c` | NV2A world and model drawing, skybox, HUD, and vblank presentation through pbGL and pbkit |
| `bake.py`, `bake_gpu.py`, `bake_lighting.py`, `assets.s` | Import and embed the original map, lighting, models, textures, and sounds |

The Xbox duel uses its own bot AI and movement code. The renderer includes the original map props, diagonal walls and ledges, and sloped floors and ceilings. The playable loadout is the rifle and pistol.

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

### Frame pacing on the original Xbox

Build `38ac0d41` was tested at 720p with one bot, an opening rifle burst, a steady right-stick turn, forward movement, and a turn in the opposite direction. The instrumented previous build (`53a596b5`) recorded ten two-refresh intervals and one three-refresh interval in 719 intervals. Two fresh launches of the revised build each recorded 719 consecutive one-refresh intervals. Worst measured intervals were 50.00 ms before and 17.06 and 16.84 ms after. Camera rotation at fixed stick deflection advanced by 0.887 degrees each frame in both revised runs. These measurements cover presentation register writes on the console. TV processing and end-to-end input latency were outside the test.

For a trace, place an empty `frame-trace.on` beside the installed XBE and start a match. After 720 frames, the title saves `frame-trace.bin` in that folder. Run `python xbox/tests/frame_trace.py frame-trace.bin` on the host to inspect refresh intervals. Add `--work input-times.bin` from the same run to measure the first death-camera and respawn frames from controller sampling through GPU completion. Remove or rename the marker before the next launch to disable recording.

On the original Xbox at 720p, build `e299b78e` took 17.26 ms of work on the first rifle death-camera frame and displayed the previous frame for two refreshes. Drawing opaque actors before the map in build `d1df0c8c` reduced that work to 15.72 ms in each of two repeats. Both 720-frame captures held one refresh throughout.

### Right-stick latency

The physical Xbox timing test pairs a changed XID report in the USB completion callback with the presentation of the first frame turning in the requested direction. Both endpoints use the Xbox performance counter. The pixel check additionally requires a change in a row of scene samples taken from the completed framebuffer. The measurements exclude stick mechanics, USB travel before the callback, panel scanout and TV processing.

With the animated bot and PC weapon camera (commit `c945d2d01`), two 720p runs gave median delays of 20.8 and 18.6 ms at 58% stick travel, and 14.3 and 20.5 ms at 15% travel. All 54 eligible onsets turned the camera, with delays from 12.0 to 31.6 ms. Each run captured 720 frames on consecutive refreshes.

| Right-stick step | Previous build | Revised run | Revised repeat with pixel check |
| --- | ---: | ---: | ---: |
| 58% travel, median USB arrival to presentation | 25.0 ms (17 turns) | 21.0 ms (18 turns) | 17.9 ms (18 turns) |
| 15% travel, turns detected within 100 ms | 0 of 4 | 4 of 4 | 4 of 4 |

In the pixel-checked run the 58% steps took 12.2–27.4 ms. Both revised captures presented 720 frames over 719 consecutive refresh intervals. The regular play XBE was `78a266fb`. The right stick uses a 12% circular deadzone. The game reserves time for a slow recent frame and samples the pad later when the GPU has headroom.

To reproduce, build with `make -C xbox AIM_TRACE=1` and place `frame-trace.on` alongside the XBE on the HDD. From the ready screen, start the duel with a rifle burst. Alternate horizontal right-stick reports of ±19000 and ±5000 with neutral gaps. Keep the game running for at least 16 seconds after the first stick pulse. Retrieve `aim-usb.bin`, `frame-trace.bin` and `input-times.bin` from the title directory, then run:

```sh
python xbox/tests/aim_latency.py aim-usb.bin frame-trace.bin --pixels input-times.bin
python xbox/tests/frame_trace.py frame-trace.bin
```

The analysis excludes capture boundaries, respawns and trials where the camera was already turning. A normal build uses the stock SDL Xbox driver.

### Camera response and motion

Build `c13dff1a` uses a 10% circular aiming deadzone and a progressive response curve. Horizontal sensitivity reaches 150 degrees per second before boost. Holding the stick at the horizontal edge ramps to 220 degrees per second over 0.4 seconds. Releasing or reversing resets the boost. Vertical sensitivity reaches 105 degrees per second with a pitch limit of 75 degrees in either direction. These values are specific to this port. The controller menu in THE FINALS was a reference for independent horizontal and vertical sensitivity, response curve and boost ramp. [Controller menu screenshot](https://cdn.mos.cms.futurecdn.net/suqdRGeWho7ieBpFEQktCT.jpg)

At 720p on the physical Xbox, a 15% stick pan moved 0.070 degrees per refresh, a 47% pan moved 0.702, and the sustained full turn moved 3.666. The previous version measured 0.092, 0.996 and 2.483 respectively. The turn sweep and a separate walking and combat replay each delivered 719 consecutive single-refresh intervals. The longest intervals were 16.97 and 16.71 ms.

On unobstructed ground, movement now follows the PC friction curve. Holding the stick reaches half the requested speed in about 82 ms. Releasing it halves the remaining speed in the same time. Air, ladders and water use their own PC friction values. The bot blends the original six running poses by distance traveled.

### Strafe tracking on ac_desert

The Xbox port shares some of the visible opponent's angular motion during small two-stick corrections. Build `6ddbe28b` gives this assistance a constant gain near the torso and gradually fades it near the edge of the aiming cone. Full horizontal stick immediately selects the ordinary turn rate.

The physical Xbox replay starts a fresh duel, holds Start for 200 ms, then alternates the horizontal left stick between +22000 and -22000 every 500 ms. The right stick follows 70 ms after each reversal at the opposite sign and magnitude 9000. Start with neutral right stick and leave fire released. Enable `frame-trace.on` and record the first 720 frames. The companion `bot-aim.bin` records the bot�s yaw and pitch in radians, player health, and the bot�s weapon sound for each frame (little-endian `<2f2i`). Run `python xbox/tests/strafe_tracking.py tracking.bin` and `python xbox/tests/frame_trace.py frame-trace.bin`.

| Opening life, visible strafing frames only | Previous run 1 / 2 | Revised run 1 / 2 |
| --- | --- | --- |
| Measured frames | 214 / 216 | 219 / 215 |
| Crosshair intersected the bot | 38.8% / 38.4% | 57.1% / 89.8% |
| Median projected distance to torso at 720p | 31.4 / 25.4 px | 16.0 / 7.6 px |
| 90th percentile distance | 119.1 / 125.9 px | 33.2 / 19.7 px |

Each capture recorded 719 consecutive single-refresh intervals. The KVM runs the same sequence in wall-clock time. Input transitions can land on different Xbox frames, so keep the individual results when comparing builds.

In a separate release test, the reticle was on the bot immediately before a full-right input. The camera turned +2.548 degrees on the first presented frame containing that input, and -2.548 degrees on the first frame containing the subsequent full-left input.

### Compare aim assist by hand

At the ready or rematch screen, press D-pad Left for Classic or Right for Tuned, then press Start. Tuned is selected on launch. Each setting stays active through the duel and subsequent rematches until changed.

Place `duel-trace.on` beside the installed XBE before launching to record each match in memory. At intermission the game writes the latest completed match for its profile to `classic.bin` or `tuned.bin`. Both recordings survive a profile switch. Retrieve them through XBMC before playing another match with the same setting. Run `python xbox/tests/duel_tracking.py classic.bin tuned.bin` to inspect direct strafe reversals, small away-input onsets and reacquisition after restored line of sight. The analyzer infers away inputs from stick direction and target bearing. A zero event count means that match did not exercise the case.
