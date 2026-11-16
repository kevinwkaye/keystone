; Kuddle installer for Windows, built by GitHub Actions with Inno Setup.
; Puts the VST3 where Ableton looks for plugins and the standalone app in Program Files.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build-kuddle\Kuddle_artefacts\Release"
#endif

[Setup]
AppId={{3F0B8C61-7A2D-4C9E-9E57-0D4B8A21C7F3}
AppName=Kuddle
AppVersion={#AppVersion}
AppPublisher=Kevin Kaye
DefaultDirName={autopf}\Kuddle
DefaultGroupName=Kuddle
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=Kuddle-{#AppVersion}-Setup
OutputDir=..\..\dist
SetupIconFile=..\..\installer\keystone.ico
UninstallDisplayIcon={app}\Kuddle.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes

[Files]
Source: "{#SourceDir}\VST3\Kuddle.vst3\*"; DestDir: "{commoncf64}\VST3\Kuddle.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\Standalone\Kuddle.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\Kuddle (standalone)"; Filename: "{app}\Kuddle.exe"
Name: "{group}\Kuddle read me"; Filename: "{app}\README.txt"
Name: "{group}\Uninstall Kuddle"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\Kuddle.vst3"
