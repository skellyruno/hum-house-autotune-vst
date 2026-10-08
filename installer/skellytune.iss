; SkellyTune — Inno Setup Installer Script
; Installs VST3 + Standalone for Windows (all DAWs)
;
; Build with: iscc installer/skellytune.iss
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)

#define MyAppName      "SkellyTune"
#define MyAppVersion   "1.0.0"
#define MyAppPublisher "HumHouse"
#define MyAppURL       "https://github.com/elijahjfrierson-prog/humhouse-autotune-vst"

[Setup]
AppId={{C2D1C9A8-C388-4582-B628-1282000F361C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
LicenseFile=eula.txt
OutputDir=..\build\installer
OutputBaseFilename=SkellyTune-Setup-{#MyAppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
PrivilegesRequired=admin
SetupIconFile=
UninstallDisplayIcon={app}\{#MyAppName}.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; VST3 plugin → system-wide VST3 folder (all DAWs: FL Studio, Ableton,
; Reaper, Cubase, Studio One, Bitwig, etc.)
Source: "..\build\HumHouseVocalTune_artefacts\Release\VST3\SkellyTune.vst3\*"; \
    DestDir: "{commoncf}\VST3\SkellyTune.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone executable
Source: "..\build\HumHouseVocalTune_artefacts\Release\Standalone\SkellyTune.exe"; \
    DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\SkellyTune.exe"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\SkellyTune.exe"; \
    Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\SkellyTune.vst3"
