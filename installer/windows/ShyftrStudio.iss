; Inno Setup script for Shyftr Studio (https://jrsoftware.org/isinfo.php)
; Build the portable folder first (scripts\build-windows.ps1 or the GitHub workflow), then:
;   ISCC.exe /DAppVersion=0.1.0 installer\windows\ShyftrStudio.iss
; Output: dist\ShyftrStudio-Setup.exe
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{6E2C7B1A-5D43-4C8E-9B1F-53A7D2C0F4A1}
AppName=Shyftr Studio
AppVersion={#AppVersion}
AppPublisher=Shyftr
AppComments=Offline video editor with local AI subtitles
DefaultDirName={autopf}\Shyftr Studio
DefaultGroupName=Shyftr Studio
DisableProgramGroupPage=yes
OutputDir=..\..\dist
OutputBaseFilename=ShyftrStudio-Setup
SetupIconFile=..\..\resources\icons\ShyftrStudio.ico
UninstallDisplayIcon={app}\Shyftr Studio.exe
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
MinVersion=10.0

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\..\dist\ShyftrStudio\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Shyftr Studio"; Filename: "{app}\Shyftr Studio.exe"
Name: "{group}\Uninstall Shyftr Studio"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Shyftr Studio"; Filename: "{app}\Shyftr Studio.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Shyftr Studio.exe"; Description: "Open Shyftr Studio"; Flags: nowait postinstall skipifsilent
