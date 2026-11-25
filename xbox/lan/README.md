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
5. Copy the development `default.xbe` and a plain-text file named `lan-server.txt` to the same folder on the Xbox. Put only the PC's IPv4 address on the first line. Launch the XBE. The Xbox joins automatically as **Xbox** with the assault rifle by default. To start with the AD-81, put `sniper` on the second line of `lan-server.txt`. During a LAN match, Y toggles the primary for your next spawn. Hold LT with the AD-81 to use its scope. To play the offline bot match again, remove the text file and relaunch.

Ammo and health pickups use the stock server's item list. If the other player takes a box first, the Xbox hides it and keeps its current inventory until the server awards another pickup.

On original hardware, both players collected the courtyard ammo box on separate spawns. The Xbox stayed at 40 reserve rounds after Windows took it, then reached 60 when the server awarded the next spawn about eight seconds later. A separate health pickup raised Xbox health from 56 to 89. A late Xbox join received an initial item list with the occupied ammo box absent.

The Xbox implementation lives under `xbox/`. Server discovery, Xbox hosting, and full stock gameplay parity are outside this milestone.
