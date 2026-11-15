; Explorer2 — Inno Setup 6 Installer Script (Windows 10/11 x64)
#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef MyAppFileVersion
  #define MyAppFileVersion "1.0.0.0"
#endif

#define MyAppName "Explorer2"
#define MyAppPublisher "openshwprojects"
#define MyAppURL "https://github.com/openshwprojects/Explorer2"
#define MyAppExeName "Explorer2.exe"

[Setup]
AppId={{8E4C7D2A-5B19-4F3E-9A62-C1D0E8F47B21}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
VersionInfoVersion={#MyAppFileVersion}
VersionInfoProductVersion={#MyAppFileVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} Setup
VersionInfoCopyright=Copyright (C) 2026 Explorer2 authors (GPL-3.0)
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=Explorer2-Setup-{#MyAppVersion}-x64
SetupIconFile=..\res\Explorer2.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"

[CustomMessages]
english.OpenInExplorer2=Open in Explorer2
english.TaskContextMenu=Add "Open in Explorer2" to folder and drive context menus
polish.OpenInExplorer2=Otwórz w Explorer2
polish.TaskContextMenu=Dodaj "Otwórz w Explorer2" do menu kontekstowego folderów i dysków

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "contextmenu"; Description: "{cm:TaskContextMenu}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\Explorer2.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Folder item right-click
Root: HKA; Subkey: "Software\Classes\Directory\shell\Explorer2"; ValueType: string; ValueName: ""; ValueData: "{cm:OpenInExplorer2}"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\Explorer2"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\Explorer2\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%V"""; Tasks: contextmenu

; Folder background right-click
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\Explorer2"; ValueType: string; ValueName: ""; ValueData: "{cm:OpenInExplorer2}"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\Explorer2"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\Explorer2\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%V"""; Tasks: contextmenu

; Drive right-click
Root: HKA; Subkey: "Software\Classes\Drive\shell\Explorer2"; ValueType: string; ValueName: ""; ValueData: "{cm:OpenInExplorer2}"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Drive\shell\Explorer2"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Drive\shell\Explorer2\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%V"""; Tasks: contextmenu

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
