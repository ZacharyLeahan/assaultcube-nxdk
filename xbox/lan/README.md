# Private LAN deathmatch

This development build joins a stock AssaultCube 1.3.0.2 dedicated server on a PC. It supports one Xbox and one PC player on `ac_desert`. The Prototype 2 release is the offline build.

1. Copy `maprot.cfg` and `serverparameters.cfg` from this folder into the Windows game's `config` directory as `xbox-lan-maprot.cfg` and `xbox-lan-parameters.cfg`.
2. In PowerShell, from the Windows game's directory, set your PC's LAN IPv4 address and run the server. The following example creates a private server identity on first use. Keep the key file local.

   ```powershell
   $lanIP = '192.168.1.210'
   $keyFile = 'config/xbox-lan-key.cfg'
   if (!(Test-Path $keyFile)) {
       $key = New-Object byte[] 32
       $rng = [Security.Cryptography.RandomNumberGenerator]::Create()
       $rng.GetBytes($key)
       $rng.Dispose()
       Set-Content -Encoding ASCII $keyFile ('-Y ' + [BitConverter]::ToString($key).Replace('-', ''))
   }
   .\bin_win32\ac_server.exe "-C$keyFile" "-i$lanIP" -f28763 -c2 -nxbox-lan -rconfig/xbox-lan-maprot.cfg -Aconfig/xbox-lan-parameters.cfg -kA0
   ```

3. Allow incoming UDP traffic to port 28763 from the Xbox on your private network. The server runs unlisted and accepts guests.
4. In the stock PC client's console, enter `/connect 192.168.1.210 28763`, substituting your PC's address.
5. Copy the development `default.xbe` and a plain-text file named `lan-server.txt` to the same folder on the Xbox. Put only the PC's IPv4 address on the first line. Launch the XBE. The Xbox joins automatically as **Xbox** with the assault rifle by default. To start with the AD-81, put `sniper` on the second line of `lan-server.txt`. During a LAN match, press Y to switch between your primary and the MK-77 pistol. Tap RT for each pistol shot and press X to reload. Press D-pad Down to choose the assault rifle or sniper rifle for your next spawn. Hold LT with the AD-81 to use its scope. To play the offline bot match again, remove the text file and relaunch.

Ammo and health pickups use the stock server's item list. If the other player takes a box first, the Xbox hides it and keeps its current inventory until the server awards another pickup.

Click the right stick to draw the knife or return to your previous firearm. Tap RT once per swing. The stock knife deals 50 damage to an unarmoured player and takes 500 ms to recover.

In a courtyard test on original hardware, both players hit once, backed out of reach, missed, and stepped in for the frag. Measured between player centers, the Xbox missed at 4.53 map units and finished at 3.17. Windows missed at 5.00 and finished at 2.79. The stock server recorded both frags.

On a stock Windows server, both players scored an MK-77 frag on original-Xbox crossplay. Holding RT fired one round. Reloading took the pistol from 9/50 to 10/49, and switching back preserved the rifle magazine. Windows received the server's shot effects for all 60 Xbox rifle rounds across three magazines.

Incoming hits add the server's push to the Xbox player's current velocity. In the flat courtyard, each of three single MK-77 hits moved a stationary Xbox player 0.3125 map units away from the shooter. Swapping roles gave the same result on all three Windows hits. Distances use settled network positions, which resolve to 1/16 of a map unit. The previous Xbox build took 18 damage with no measured displacement.

Both clients also took an 18-damage hit while strafing around the courtyard corner. In a two-second window around the hit, the central gameplay image changed on all 120 consecutive frame comparisons in the Xbox HDMI capture. The equivalent pass without a shot changed on all 119 comparisons.

On original hardware, both players collected the courtyard ammo box on separate spawns. The Xbox stayed at 40 reserve rounds after Windows took it, then reached 60 when the server awarded the next spawn about eight seconds later. A separate health pickup raised Xbox health from 56 to 89. A late Xbox join received an initial item list with the occupied ammo box absent.

The Xbox implementation lives under `xbox/`. Server discovery, Xbox hosting, and full stock gameplay parity are outside this milestone.
