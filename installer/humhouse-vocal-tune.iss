; HumHouse Vocal Tune — Inno Setup Installer Script
; Installs VST3 + Standalone for Windows (all DAWs)
;
; Build with: iscc installer/humhouse-vocal-tune.iss
; Requires Inno Setup 6+ (https://jrsoftware.org/isinfo.php)

#define MyAppName      "HumHouse Vocal Tune"
#define MyAppVersion   "1.0.0"
#define MyAppPublisher "HumHouse"
#define MyAppURL       "https://github.com/elijahjfrierson-prog/humhouse-autotune-vst"

[Setup]
AppId={{A2B3C4D5-E6F7-8901-2345-6789ABCDEF01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
DefaultDirName={autopf}\HumHouse\{#MyAppName}
DefaultGroupName={#MyAppName}
LicenseFile=eula.txt
OutputDir=..\build\installer
OutputBaseFilename=HumHouse-VocalTune-Setup-{#MyAppVersion}
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
Source: "..\build\HumHouseVocalTune_artefacts\Release\VST3\HumHouse Vocal Tune.vst3\*"; \
    DestDir: "{commoncf}\VST3\HumHouse Vocal Tune.vst3"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

; Standalone executable
Source: "..\build\HumHouseVocalTune_artefacts\Release\Standalone\HumHouse Vocal Tune.exe"; \
    DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\HumHouse Vocal Tune.exe"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\HumHouse Vocal Tune.exe"; \
    Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\HumHouse Vocal Tune.vst3"
