# Keystone: Update firmware, started from the phone.
#
# Keystone saves the new installer in Documents\Keystone\Updates, writes request.json beside
# it and starts the "Keystone Update" scheduled task, which runs this with administrator
# rights (made by the installer, so no Windows prompt on a laptop with no screen).
#
#  1. Checks the installer is a Keystone release: against GitHub when the internet is
#     there, and it must be one of Keystone's own file names in the Updates folder.
#  2. Keeps the installer for the version running now, so there's always a way back.
#  3. Installs quietly, starts Keystone again and waits for it to say it's running.
#  4. If the new version doesn't start inside 90 seconds, puts the old one back.
#
# Everything it does goes in Documents\Keystone\Updates\update.log.
param([switch]$Register)

$repo = 'kevinwkaye/keystone'

# ---- run by the installer: make the scheduled task
if ($Register) {
    $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$PSCommandPath`""
    # whoever is signed in, with their full rights; at sign-in too, to finish an update cut short
    $principal = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 15) -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName 'Keystone Update' -Action $action -Principal $principal -Trigger $trigger -Settings $settings -Force | Out-Null
    exit 0
}

$home2 = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Keystone'
$updates = Join-Path $home2 'Updates'
$request = Join-Path $updates 'request.json'
if (-not (Test-Path $request)) { exit 0 }

$logFile = Join-Path $updates 'update.log'
function Log($what) { Add-Content -Path $logFile -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $what" }
$exe = Join-Path (Split-Path $PSScriptRoot -Parent) 'Keystone.exe'
function Start-Keystone {
    # through Explorer, so Keystone runs as you and not with administrator rights
    Start-Process explorer.exe -ArgumentList "`"$exe`""
}
function Started-Since($version, $since) {
    $f = Join-Path $home2 'started.json'
    if (-not (Test-Path $f)) { return $false }
    try { $s = Get-Content $f -Raw | ConvertFrom-Json } catch { return $false }
    return ($s.version -eq $version -and [int64]$s.at -ge $since)
}
function Wait-For($version, $since) {
    for ($i = 0; $i -lt 90; $i++) {
        if (Started-Since $version $since) { return $true }
        Start-Sleep -Seconds 1
    }
    return $false
}
function Install($installer) {
    $p = Start-Process $installer -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-' -Wait -PassThru
    return $p.ExitCode
}

try { $r = Get-Content $request -Raw | ConvertFrom-Json } catch { Log "Couldn't read the request: $($_.Exception.Message)"; Remove-Item $request -Force; exit 1 }
Remove-Item $request -Force
$version = [string]$r.version
$from = [string]$r.from
Log "Update firmware: $from to $version"

# ---- 1. is it a Keystone release?
$installer = [IO.Path]::GetFullPath([string]$r.installer)
$name = Split-Path $installer -Leaf
if (-not $installer.StartsWith($updates + '\', [StringComparison]::OrdinalIgnoreCase) -or $name -notmatch '^Keystone-[0-9.]+-Setup\.exe$' -or -not (Test-Path $installer)) {
    Log "Refused: $installer isn't a Keystone installer in the Updates folder"
    exit 1
}
$have = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLower()
try {
    $rel = Invoke-RestMethod "https://api.github.com/repos/$repo/releases/tags/v$version" -TimeoutSec 15 -Headers @{ 'User-Agent' = 'Keystone' }
    $asset = $rel.assets | Where-Object { $_.name -eq $name } | Select-Object -First 1
    if (-not $asset) { Log "Refused: GitHub has no $name in release v$version"; Remove-Item $installer -Force; exit 1 }
    if ($asset.digest) {
        if (($asset.digest -replace '^sha256:', '') -ne $have) { Log "Refused: $name doesn't match the one on GitHub"; Remove-Item $installer -Force; exit 1 }
        Log "Matches the release on GitHub"
    }
} catch {
    Log "GitHub not reachable, going on the phone's checksum ($have)"
}

# ---- 2. a way back: the installer for the version running now
$back = Join-Path $updates "Keystone-$from-Setup.exe"
if (-not (Test-Path $back) -and $from) {
    try {
        Invoke-WebRequest "https://github.com/$repo/releases/download/v$from/Keystone-$from-Setup.exe" -OutFile $back -TimeoutSec 120 -UseBasicParsing
        Log "Kept $from to go back to"
    } catch {
        if (Test-Path $back) { Remove-Item $back -Force }
        Log "No way back to $from (no installer kept and GitHub not reachable): going ahead anyway"
    }
}

# ---- 3. install and start
$since = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$code = Install $installer
Log "Installer finished ($code)"
Start-Keystone
if (Wait-For $version $since) {
    Log "Keystone $version is running"
    # keep this installer and the one before it, nothing older
    Get-ChildItem $updates -Filter 'Keystone-*-Setup.exe' | Where-Object { $_.Name -ne $name -and $_.FullName -ne $back } | Remove-Item -Force -ErrorAction SilentlyContinue
    exit 0
}

# ---- 4. it didn't start: put the old one back
Log "Keystone $version didn't start inside 90 seconds"
if (Test-Path $back) {
    Get-Process Keystone -ErrorAction SilentlyContinue | Stop-Process -Force
    $since = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $code = Install $back
    Start-Keystone
    if (Wait-For $from $since) { Log "Back on $from" } else { Log "$from didn't start either: check keystone.log" }
} else {
    Start-Keystone
    Log "Nothing to go back to"
}
exit 1
