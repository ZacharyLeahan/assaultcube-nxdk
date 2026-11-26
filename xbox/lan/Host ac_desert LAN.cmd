@echo off
setlocal
cd /d "%~dp0"
if not exist bin_win32\ac_server.exe goto :missing
if not exist bin_win32\ac_client.exe goto :missing
if not exist XboxLAN\maprot.cfg goto :missing
if not exist XboxLAN\serverparameters.cfg goto :missing
if not exist XboxLAN\private-key.cfg (
    powershell -NoProfile -Command "$k=New-Object byte[] 32; $r=[Security.Cryptography.RandomNumberGenerator]::Create(); $r.GetBytes($k); $r.Dispose(); [IO.File]::WriteAllText('XboxLAN\private-key.cfg','-Y '+[BitConverter]::ToString($k).Replace('-',''))"
    if errorlevel 1 goto :error
)
start "AssaultCube private LAN server" "bin_win32\ac_server.exe" -CXboxLAN/private-key.cfg -c2 -nac_desert-LAN -rXboxLAN/maprot.cfg -AXboxLAN/serverparameters.cfg -kA0
timeout /t 2 /nobreak >nul
start "" "bin_win32\ac_client.exe" "--home=?MYDOCUMENTS?\My Games\AssaultCube\v1.3" --init "assaultcube://127.0.0.1:28763"
exit /b 0

:missing
echo Extract the entire Windows host ZIP into the AssaultCube 1.3.0.2 game folder.
:error
pause
exit /b 1
