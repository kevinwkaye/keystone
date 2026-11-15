; Keystone installer for Windows 11, built by GitHub Actions with Inno Setup.
; Installs the app, starts it with Windows, lets it run with the lid closed, and opens the
; firewall so the phone can find it on the hotspot.

#ifndef AppVersion
  #define AppVersion "0.1.5"
#endif
#ifndef SourceDir
  #define SourceDir "..\build\app\Keystone_artefacts\Release"
#endif

[Setup]
AppId={{6C7E2A4B-9F1D-4E5A-B8C3-4B57FF5DCAA5}
AppName=Keystone
AppVersion={#AppVersion}
AppPublisher=Kevin Kaye
DefaultDirName={autopf}\Keystone
DefaultGroupName=Keystone
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=Keystone-{#AppVersion}-Setup
OutputDir=..\dist
SetupIconFile=keystone.ico
UninstallDisplayIcon={app}\Keystone.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no

[Tasks]
Name: "startup"; Description: "Start Keystone when Windows starts"; Flags: checkedonce
Name: "lid"; Description: "Keep playing with the lid closed (sets the lid to Do nothing when closed)"; Flags: checkedonce
Name: "power"; Description: "Keep USB audio and Wi-Fi at full power (avoids dropouts and link lag)"; Flags: checkedonce

[Files]
Source: "{#SourceDir}\Keystone.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\Keystone"; Filename: "{app}\Keystone.exe"
Name: "{group}\Keystone folder (captures, log, songs)"; Filename: "{userdocs}\Keystone"
Name: "{group}\Uninstall Keystone"; Filename: "{uninstallexe}"

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "Keystone"; ValueData: """{app}\Keystone.exe"" --background"; Flags: uninsdeletevalue; Tasks: startup

[Run]
; firewall: one rule for the app covers the phone link (TCP 47801) and discovery (UDP 47800) on every network type
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""Keystone"""; Flags: runhidden waituntilterminated
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""Keystone"" dir=in action=allow program=""{app}\Keystone.exe"" enable=yes profile=any"; Flags: runhidden waituntilterminated
; lid closed: do nothing, on mains and battery
Filename: "{sys}\powercfg.exe"; Parameters: "/setacvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0"; Flags: runhidden waituntilterminated; Tasks: lid
Filename: "{sys}\powercfg.exe"; Parameters: "/setdcvalueindex SCHEME_CURRENT SUB_BUTTONS LIDACTION 0"; Flags: runhidden waituntilterminated; Tasks: lid
; USB selective suspend off, wireless adapter at maximum performance
Filename: "{sys}\powercfg.exe"; Parameters: "/setacvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0"; Flags: runhidden waituntilterminated; Tasks: power
Filename: "{sys}\powercfg.exe"; Parameters: "/setdcvalueindex SCHEME_CURRENT 2a737441-1930-4402-8d77-b2bebba308a3 48e6b7a6-50f5-4782-a5d4-53bb8f07e226 0"; Flags: runhidden waituntilterminated; Tasks: power
Filename: "{sys}\powercfg.exe"; Parameters: "/setacvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0"; Flags: runhidden waituntilterminated; Tasks: power
Filename: "{sys}\powercfg.exe"; Parameters: "/setdcvalueindex SCHEME_CURRENT 19cbb8fa-5279-450e-9fac-8a3d5fedd0c1 12bbebe6-58d6-4636-95bb-3217ef867c1a 0"; Flags: runhidden waituntilterminated; Tasks: power
Filename: "{sys}\powercfg.exe"; Parameters: "/setactive SCHEME_CURRENT"; Flags: runhidden waituntilterminated
Filename: "{app}\Keystone.exe"; Description: "Start Keystone now"; Flags: postinstall nowait skipifsilent

[UninstallRun]
Filename: "{sys}\taskkill.exe"; Parameters: "/im Keystone.exe /f"; Flags: runhidden; RunOnceId: "StopKeystone"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""Keystone"""; Flags: runhidden; RunOnceId: "DelFirewall"

[Code]
// stop a running Keystone so its file can be replaced
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Code: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/im Keystone.exe /f', '', SW_HIDE, ewWaitUntilTerminated, Code);
  Sleep(500);
  Result := '';
end;
