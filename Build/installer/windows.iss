; Inno Setup script for Parametric NAM Studio (64-bit Windows 10/11)
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{5C3A0D6E-7F1B-4B8E-9A55-2E6B1D8C4F21}
AppName=Parametric NAM Studio
AppVersion={#AppVersion}
AppPublisher=Parametric NAM Studio
AppPublisherURL=https://github.com/vfxchris581/nam-parametric
DefaultDirName={autopf}\Parametric NAM Studio
DefaultGroupName=Parametric NAM Studio
OutputBaseFilename=ParametricNAMStudio-{#AppVersion}-Windows-Setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile=..\..\Resources\icon.ico
UninstallDisplayIcon={app}\Parametric NAM Studio.exe
ChangesAssociations=yes

[Files]
Source: "..\..\build\Source\ParametricNAMStudio_artefacts\Release\Parametric NAM Studio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\build\Source\cli\Release\pnam.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "..\..\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Parametric NAM Studio"; Filename: "{app}\Parametric NAM Studio.exe"
Name: "{autodesktop}\Parametric NAM Studio"; Filename: "{app}\Parametric NAM Studio.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Registry]
Root: HKA; Subkey: "Software\Classes\.pnampreset"; ValueType: string; ValueName: ""; ValueData: "ParametricNAMStudio.Preset"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\ParametricNAMStudio.Preset"; ValueType: string; ValueName: ""; ValueData: "Parametric NAM Studio preset"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\ParametricNAMStudio.Preset\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Parametric NAM Studio.exe"" ""%1"""

[Run]
Filename: "{app}\Parametric NAM Studio.exe"; Description: "Launch Parametric NAM Studio"; Flags: nowait postinstall skipifsilent
