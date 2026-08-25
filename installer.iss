; E2E4 Soft — Game Network Optimizer v3.0.0
; Inno Setup Script

[Setup]
AppName=E2E4 Soft Game Network Optimizer
AppVersion=3.0.0
AppVerName=E2E4 Soft GNO 3.0.0
AppPublisher=Reagent Network Service e2E4
AppPublisherURL=https://github.com/Reagent420/e2e4-soft
AppSupportURL=https://github.com/Reagent420/e2e4-soft/issues
DefaultDirName={autopf}\E2E4 Soft GNO
DefaultGroupName=E2E4 Soft GNO
LicenseFile=LICENSE
OutputDir=.
OutputBaseFilename=GNO-Setup-v3.0.0
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayIcon={app}\GNO.exe
SetupIconFile=icon.ico

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isdl"

[CustomMessages]
CreateDesktopIcon=Create a &desktop shortcut
LaunchProgram=&Launch E2E4 Soft GNO

[Files]
Source: "deploy\v3.0.0\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\E2E4 Soft GNO"; Filename: "{app}\GNO.exe"
Name: "{group}\{cm:UninstallProgram,E2E4 Soft GNO}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\E2E4 Soft GNO"; Filename: "{app}\GNO.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Run]
Filename: "{app}\GNO.exe"; Description: "{cm:LaunchProgram,E2E4 Soft GNO}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\platforms"
Type: filesandordirs; Name: "{app}\imageformats"
Type: filesandordirs; Name: "{app}\styles"
