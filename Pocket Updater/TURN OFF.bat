@echo off
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\Lantern Deep Pocket Updater.lnk" 2>nul
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='powershell.exe'\" | Where-Object { $_.CommandLine -like '*pocket_updater.ps1*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
echo The Pocket updater is turned off.
pause
