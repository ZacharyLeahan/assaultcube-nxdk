# Private LAN deathmatch

Play a two-player deathmatch on a stock Windows AssaultCube 1.3.0.2 server. Connect the Xbox and PC to the same LAN.

1. Extract [Windows-LAN-host.zip](https://github.com/ZacharyLeahan/assaultcube-nxdk/releases/download/xbox-prototype-3/Windows-LAN-host.zip) into the Windows game folder, beside `assaultcube.bat`.
2. Double-click **Host ac_desert LAN.cmd**. It starts the stock dedicated server and opens the Windows client. Complete any first-run or player login prompts. Leave the server window open while playing. If you already have the client open, its console command is `/connect 127.0.0.1 28763`.
3. Allow the server on your private network if Windows Firewall prompts. Discovery uses UDP 28762, gameplay uses 28763, and server information uses 28764.
4. Launch the [Xbox XBE](https://github.com/ZacharyLeahan/assaultcube-nxdk/releases/download/xbox-prototype-3/default.xbe). Press **A** for **Join LAN Game**, select **ac_desert-LAN** with the D-pad, and press **A** again. The list refreshes automatically. **B** goes back and **Start** plays the offline bot match.

For direct connection on the default port, put the PC's LAN IPv4 address on the first line of `lan-server.txt` beside the XBE. Remove that file to start at the normal ready screen.

The default primary is the MTP-57 rifle. Set the second line of `lan-server.txt` to `assault`, `sniper`, `subgun`, `carbine`, or `shotgun` to choose a starting weapon. During the match, press D-pad Down to cycle those five choices for your next spawn. Press Y to switch between your current primary and the MK-77 pistol. Hold RT for automatic fire with the rifle or SMG. Tap RT for each carbine, sniper, shotgun, or pistol shot. Press X to reload or hold LT to use the sniper scope.

On original hardware, the Xbox timestamped all 30 shots in an SMG magazine 80 ms apart, and Windows received all 30 from the stock server. Firing wide left the opponent at full health. After reloading, seven 16-damage hits scored the frag. Both clients also fired a single carbine round on a held trigger for 60 damage.

In a V-19 shotgun duel on `ac_desert`, the stock server registered a miss, a partial pellet hit, and a close-range frag from each client. Partial hits dealt 33 damage from Xbox and 41 from Windows. At close range, both landed all 63 pellets for 155 damage.

Press D-pad Right to equip a collected grenade. Hold RT to pull the pin and release to throw. In a courtyard exchange, the stock server credited the Xbox throw with 36 damage to Windows and 39 to the thrower. A cooked Windows throw dealt 10 damage to Xbox and killed the thrower.

Hold D-pad Left to see each player's frags, deaths and ping. The scoreboard opens at intermission and clears when the server starts the next match.

The akimbo pickup equips two MK-77s for 30 seconds. Hold RT for automatic fire, press X to reload, or use Y to switch back to your primary. Both clients scored a dual-pistol frag in the same hardware match. The stock server accepted Xbox shots spaced 80 ms apart, and Windows took an unarmoured Xbox player from 100 health to a frag in six 18-damage hits.

Ammo and health pickups use the stock server's item list. If the other player takes a box first, the Xbox hides it and keeps its current inventory until the server awards another pickup.

Click the right stick to draw the knife or return to your previous firearm. Tap RT once per swing. The stock knife deals 50 damage to an unarmoured player and takes 500 ms to recover.

In a courtyard test on original hardware, both players hit once, backed out of reach, missed, and stepped in for the frag. Measured between player centers, the Xbox missed at 4.53 map units and finished at 3.17. Windows missed at 5.00 and finished at 2.79. The stock server recorded both frags.

On a stock Windows server, both players scored an MK-77 frag on original-Xbox crossplay. Holding RT fired one round. Reloading took the pistol from 9/50 to 10/49, and switching back preserved the rifle magazine. Windows received the server's shot effects for all 60 Xbox rifle rounds across three magazines.

Incoming hits add the server's push to the Xbox player's current velocity. In the flat courtyard, each of three single MK-77 hits moved a stationary Xbox player 0.3125 map units away from the shooter. Swapping roles gave the same result on all three Windows hits. Distances use settled network positions, which resolve to 1/16 of a map unit. The previous Xbox build took 18 damage with no measured displacement.

Both clients also took an 18-damage hit while strafing around the courtyard corner. In a two-second window around the hit, the central gameplay image changed on all 120 consecutive frame comparisons in the Xbox HDMI capture. The equivalent pass without a shot changed on all 119 comparisons.

On original hardware, both players collected the courtyard ammo box on separate spawns. The Xbox stayed at 40 reserve rounds after Windows took it, then reached 60 when the server awarded the next spawn about eight seconds later. A separate health pickup raised Xbox health from 56 to 89. A late Xbox join received an initial item list with the occupied ammo box absent.

The Xbox implementation lives under `xbox/`. The discovery and join flow was tested in xemu and xbased using virtual Ethernet connected to stock Windows servers. The original-hardware gameplay tests above used direct IP.
