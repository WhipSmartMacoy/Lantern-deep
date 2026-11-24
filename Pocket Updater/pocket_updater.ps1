# Lantern Deep - Pocket updater.
# Waits in the background. When the Analogue Pocket's SD card shows up as a drive, asks "Update files?" and,
# on Yes, copies everything in "..\Pocket SD" onto the card. Saves are never touched and nothing is deleted.
Add-Type -AssemblyName System.Windows.Forms
$created = $false
$mutex = New-Object System.Threading.Mutex($true, 'LanternDeepPocketUpdater', [ref]$created)
if (-not $created) { exit }   # already running

$src = Join-Path (Split-Path -Parent $PSScriptRoot) 'Pocket SD'
$log = Join-Path $PSScriptRoot 'last_update_log.txt'
$sys = $env:SystemDrive + '\'

function Is-PocketCard($root) {
    (Test-Path (Join-Path $root 'Cores')) -and (Test-Path (Join-Path $root 'Platforms')) -and (Test-Path (Join-Path $root 'Assets'))
}
function Files-To-Copy($root) {   # what robocopy would copy (new or changed files only)
    $out = & robocopy $src $root /E /L /FFT /XD Saves /NJH /NJS /NDL /NC /NS /NP
    @($out | ForEach-Object { "$_".Trim() } | Where-Object { $_ -ne '' })
}
function Ask($text, $buttons, $icon) {
    $owner = New-Object System.Windows.Forms.Form
    $owner.TopMost = $true
    $r = [System.Windows.Forms.MessageBox]::Show($owner, $text, 'Lantern Deep', $buttons, $icon)
    $owner.Dispose()
    $r
}

$seen = @{}
while ($true) {
    $now = @{}
    try {
        $drives = [System.IO.DriveInfo]::GetDrives() | Where-Object { $_.IsReady -and $_.Name -ne $sys -and ($_.DriveType -eq 'Removable' -or $_.DriveType -eq 'Fixed') }
        foreach ($d in $drives) {
            $root = $d.Name
            if (-not (Is-PocketCard $root)) { continue }
            $now[$root] = $true
            if ($seen.ContainsKey($root)) { continue }   # already handled since it was plugged in
            if (-not (Test-Path $src)) { continue }
            $files = Files-To-Copy $root
            if ($files.Count -eq 0) {
                [void](Ask "Pocket card found ($root).`n`nLantern Deep is already up to date on it." 'OK' 'Information')
                continue
            }
            $names = ($files | Select-Object -First 8 | ForEach-Object { '  ' + (Split-Path $_ -Leaf) }) -join "`n"
            if ($files.Count -gt 8) { $names += "`n  ... and " + ($files.Count - 8) + ' more' }
            $r = Ask "Update files?`n`nPocket card found ($root). $($files.Count) Lantern Deep file(s) are new or changed:`n$names`n`nYour saves are not touched." 'YesNo' 'Question'
            if ($r -eq 'Yes') {
                & robocopy $src $root /E /FFT /XD Saves /R:2 /W:1 /NP "/LOG:$log" | Out-Null
                if ($LASTEXITCODE -ge 8) {
                    [void](Ask "Some files could not be copied.`nSee last_update_log.txt in the Pocket Updater folder." 'OK' 'Warning')
                } else {
                    [void](Ask "Done - $($files.Count) file(s) copied.`n`nEject the card safely before removing it." 'OK' 'Information')
                }
            }
        }
    } catch { }
    $seen = $now
    Start-Sleep -Seconds 3
}
