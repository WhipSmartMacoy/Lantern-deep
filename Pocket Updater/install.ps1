# Makes the Pocket updater start with Windows, and starts it now.
$script = Join-Path $PSScriptRoot 'pocket_updater.ps1'
$args2 = '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' + $script + '"'
$lnk = Join-Path ([Environment]::GetFolderPath('Startup')) 'Lantern Deep Pocket Updater.lnk'
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$s.TargetPath = 'powershell.exe'
$s.Arguments = $args2
$s.WindowStyle = 7
$s.Description = 'Asks to update Lantern Deep when the Pocket SD card is connected'
$s.Save()
Start-Process powershell.exe -ArgumentList $args2 -WindowStyle Hidden
Write-Host 'Installed. The updater is running and will start with Windows.'
