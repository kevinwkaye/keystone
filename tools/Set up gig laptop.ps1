# Keystone: set this laptop up as a gig box.
#  - stays on with the lid closed, never sleeps on its own
#  - the power button puts it to sleep, and wakes it again
#  - no sign-in after waking, and signs in by itself when it starts
#  - Keystone starts straight away after sign-in
# Run it once. It asks for administrator rights, then opens one Windows box for your password.

# ask for administrator rights
$me = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $me.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Start-Process powershell.exe "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`"" -Verb RunAs
    exit
}

$log = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Keystone\laptop setup.txt'
$lines = @("Keystone gig laptop setup, $(Get-Date)")
function Step($what, [scriptblock]$do) {
    try { & $do | Out-Null; $script:lines += "OK      $what" }
    catch { $script:lines += "FAILED  $what : $($_.Exception.Message)" }
}

# ---- power: lid does nothing, no timed sleep, power button sleeps
Step 'Lid closed does nothing (mains and battery)' { powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0; powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0 }
Step 'Power button sleeps (mains and battery)'   { powercfg /setacvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 1; powercfg /setdcvalueindex SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION 1 }
Step 'Never sleep or hibernate on a timer'       { powercfg /change standby-timeout-ac 0; powercfg /change standby-timeout-dc 0; powercfg /change hibernate-timeout-ac 0; powercfg /change hibernate-timeout-dc 0 }
Step 'No sign-in when waking from sleep'         { powercfg /setacvalueindex SCHEME_CURRENT SUB_NONE CONSOLELOCK 0; powercfg /setdcvalueindex SCHEME_CURRENT SUB_NONE CONSOLELOCK 0 }
Step 'USB audio and footswitch never suspended'  { powercfg /setacvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0; powercfg /setdcvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0 }
Step 'Wi-Fi at full power (steady phone link)'   { powercfg /setacvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0; powercfg /setdcvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0 }
Step 'Best performance when plugged in (no slow cores for audio)' { powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR PROCTHROTTLEMIN 100; powercfg /overlaysetactive ded574b5-45a0-4f42-8737-46345c09c238 }
Step 'Apply the power plan'                       { powercfg /setactive SCHEME_CURRENT }

# ---- sign-in
# Windows hides the "sign in automatically" option while Windows Hello is required
Step 'Allow automatic sign-in (Windows Hello not required)' {
    $k = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\PasswordLess\Device'
    if (-not (Test-Path $k)) { New-Item -Path $k -Force }
    Set-ItemProperty -Path $k -Name DevicePasswordLessBuildVersion -Value 0 -Type DWord
}
Step 'No screen saver lock' {
    Set-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name ScreenSaveActive -Value '0'
    Set-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name ScreenSaverIsSecure -Value '0'
}
Step 'Dynamic lock off' { Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows NT\CurrentVersion\Winlogon' -Name EnableGoodbye -Value 0 -Type DWord }

# ---- Keystone straight after sign-in
Step 'Start Keystone at sign-in' {
    $exe = Join-Path $env:ProgramFiles 'Keystone\Keystone.exe'
    if (-not (Test-Path $exe)) { throw "Keystone isn't installed at $exe" }
    Set-ItemProperty -Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name Keystone -Value "`"$exe`" --background"
}
Step 'No startup delay for Keystone' {
    $k = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\Serialize'
    if (-not (Test-Path $k)) { New-Item -Path $k -Force }
    Set-ItemProperty -Path $k -Name StartupDelayInMSec -Value 0 -Type DWord
}

# ---- no surprise restarts for updates while it's switched on
Step 'Windows Update never restarts it while signed in' {
    $k = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate\AU'
    if (-not (Test-Path $k)) { New-Item -Path $k -Force }
    Set-ItemProperty -Path $k -Name NoAutoRebootWithLoggedOnUsers -Value 1 -Type DWord
}

$lines += ''
$lines += 'Power plan now:'
$lines += (powercfg /query SCHEME_CURRENT SUB_BUTTONS LIDACTION | Select-String 'Current (AC|DC) Power Setting Index')
$lines += (powercfg /query SCHEME_CURRENT SUB_BUTTONS PBUTTONACTION | Select-String 'Current (AC|DC) Power Setting Index')
$lines | Set-Content -Path $log -Encoding UTF8

# ---- the one step that needs you: your password, typed into Windows' own box
Add-Type -AssemblyName PresentationFramework
[System.Windows.MessageBox]::Show(
"Power and startup are set.`n`nLast step: a Windows box opens next.`n`n1. Untick 'Users must enter a user name and password to use this computer'.`n2. Press OK.`n3. Type your Windows password twice (the one for $env:USERNAME, not a PIN).`n`nIf there's no tick box, restart the laptop and run this again.",
'Keystone gig laptop') | Out-Null
Start-Process netplwiz
