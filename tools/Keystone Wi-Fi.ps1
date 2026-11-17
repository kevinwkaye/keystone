# Keystone's own Wi-Fi on the laptop: a network called Keystone that the phone joins, so
# nobody has to switch a phone hotspot on. Keystone starts this hidden when it starts and
# stops it when it closes or you switch the network off on the phone.
#
# Two ways, tried in order:
#   1. Wi-Fi Direct in its "legacy" mode: an ordinary Wi-Fi network that needs no internet
#      and no other connection, which suits a rehearsal room.
#   2. Windows' own Mobile hotspot, if the first won't start. Windows only allows this while
#      the laptop has another network (home Wi-Fi, a cable), so it's the fallback.
#
# It keeps an eye on the network every 15 seconds, stops when Keystone does, and starts it again if Windows stops it.
# What's happening goes in Documents\Keystone\wifi-status.txt, which Keystone shows on the
# phone's Rig page.
param(
    [string]$Ssid = 'Keystone',
    [Parameter(Mandatory = $true)][string]$Pass,
    [string]$StatusFile = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Keystone\wifi-status.txt'),
    [int]$ParentPid = 0
)
$ErrorActionPreference = 'Stop'

function Say($text) {
    try { Set-Content -Path $StatusFile -Value $text -Encoding UTF8 } catch { }
}

# WinRT from PowerShell: wait for its async calls
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
$asTaskAction = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' })[0]
function Await($op, [Type]$type) {
    $task = $asTaskGeneric.MakeGenericMethod($type).Invoke($null, @($op))
    $task.Wait(-1) | Out-Null
    $task.Result
}
function AwaitAction($op) {
    $task = $asTaskAction.Invoke($null, @($op))
    $task.Wait(-1) | Out-Null
}

$null = [Windows.Devices.WiFiDirect.WiFiDirectAdvertisementPublisher, Windows.Devices.WiFiDirect, ContentType = WindowsRuntime]
$null = [Windows.Security.Credentials.PasswordCredential, Windows.Security.Credentials, ContentType = WindowsRuntime]
$null = [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]
$null = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime]

# ---- 1. Wi-Fi Direct, legacy mode
function Start-Direct {
    $pub = New-Object Windows.Devices.WiFiDirect.WiFiDirectAdvertisementPublisher
    $pub.Advertisement.IsAutonomousGroupOwnerEnabled = $true
    $pub.Advertisement.LegacySettings.IsEnabled = $true
    $pub.Advertisement.LegacySettings.Ssid = $Ssid
    $cred = New-Object Windows.Security.Credentials.PasswordCredential
    $cred.Password = $Pass
    $pub.Advertisement.LegacySettings.Passphrase = $cred
    $pub.Start()
    for ($i = 0; $i -lt 20 -and $pub.Status -eq 'Created'; $i++) { Start-Sleep -Milliseconds 250 }
    if ($pub.Status -ne 'Started') { throw "Wi-Fi Direct said $($pub.Status)" }
    return $pub
}

# ---- 2. Windows' Mobile hotspot, on whatever connection the laptop has
function Start-Hotspot {
    $profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
    if (-not $profile) { $profile = [Windows.Networking.Connectivity.NetworkInformation]::GetConnectionProfiles() | Select-Object -First 1 }
    if (-not $profile) { throw 'Windows needs another network before it will start Mobile hotspot' }
    $tm = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
    $cfg = $tm.GetCurrentAccessPointConfiguration()
    if ($cfg.Ssid -ne $Ssid -or $cfg.Passphrase -ne $Pass) {
        $cfg.Ssid = $Ssid
        $cfg.Passphrase = $Pass
        AwaitAction ($tm.ConfigureAccessPointAsync($cfg))
    }
    if ($tm.TetheringOperationalState -ne 'On') {
        $r = Await ($tm.StartTetheringAsync()) ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])
        if ($r.Status -ne 'Success') { throw "Mobile hotspot said $($r.Status) $($r.AdditionalErrorMessage)" }
    }
    return $tm
}

$direct = $null
$hotspot = $null
$why = ''
while ($true) {
    # Keystone has gone (closed, or crashed and starting again): stop, so two never run at once
    if ($ParentPid -and -not (Get-Process -Id $ParentPid -ErrorAction SilentlyContinue)) {
        if ($direct) { $direct.Stop() }
        Say 'Off'
        exit 0
    }
    try {
        if ($direct -and $direct.Status -ne 'Started') { $direct = $null }
        if ($hotspot -and $hotspot.TetheringOperationalState -ne 'On') { $hotspot = $null }
        if (-not $direct -and -not $hotspot) {
            try {
                $direct = Start-Direct
                Say "On: $Ssid (Wi-Fi Direct)"
            } catch {
                $why = $_.Exception.Message
                try {
                    $hotspot = Start-Hotspot
                    Say "On: $Ssid (Mobile hotspot)"
                } catch {
                    Say "Off: couldn't start it. $why. $($_.Exception.Message)"
                }
            }
        }
    } catch {
        Say "Off: $($_.Exception.Message)"
    }
    for ($i = 0; $i -lt 15; $i++) {
        Start-Sleep -Seconds 1
        if ($ParentPid -and -not (Get-Process -Id $ParentPid -ErrorAction SilentlyContinue)) { break }
    }
}
