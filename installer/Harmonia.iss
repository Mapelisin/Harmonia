; Harmonia's installer (Inno Setup 6). tools\make-installer.cmd builds it and passes:
;   SourceExe  - the Release Harmonia.exe (all it needs is inside it, the plugin processes too)
;   IconFile   - the program's icon (.ico) for the installer itself
;   OutputDir  - where Harmonia-Setup-<version>.exe goes
; The version comes from Harmonia.exe, so it is set in one place: CMakeLists.txt.

#ifndef SourceExe
  #error Pass /DSourceExe=<path to Harmonia.exe> (tools\make-installer.cmd does)
#endif

#define AppVersion GetStringFileInfo(SourceExe, "ProductVersion")
#if AppVersion == ""
  #error Harmonia.exe has no version
#endif

[Setup]
; Never change it: an update finds the installed Harmonia by it (tools\test-installer.ps1 reads it too).
AppId={{620F6DBF-9E8B-4103-A82F-EBF5DD64FEB4}
AppName=Harmonia
AppVersion={#AppVersion}
AppVerName=Harmonia {#AppVersion}
AppPublisher=Harmonia
AppPublisherURL=https://github.com/Mapelisin/Harmonia
AppSupportURL=https://github.com/Mapelisin/Harmonia/issues
AppUpdatesURL=https://github.com/Mapelisin/Harmonia
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Harmonia
DefaultGroupName=Harmonia
DisableProgramGroupPage=yes
; For everyone on the computer (asks for administrator rights) or only for this user: the first page asks.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
ChangesAssociations=yes
; A running Harmonia (and its plugin processes) is closed before its file is replaced.
CloseApplications=yes
RestartApplications=no
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\Harmonia.exe
UninstallDisplayName=Harmonia
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
OutputDir={#OutputDir}
OutputBaseFilename=Harmonia-Setup-{#AppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[CustomMessages]
english.ProjectFile=Harmonia project
russian.ProjectFile=Проект Harmonia
english.OpenProjects=Open .harmonia projects with Harmonia (double click)
russian.OpenProjects=Открывать проекты .harmonia в Harmonia (двойным кликом)
english.FileTypes=Files:
russian.FileTypes=Файлы:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "associate"; Description: "{cm:OpenProjects}"; GroupDescription: "{cm:FileTypes}"

[Files]
Source: "{#SourceExe}"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Harmonia"; Filename: "{app}\Harmonia.exe"
Name: "{autodesktop}\Harmonia"; Filename: "{app}\Harmonia.exe"; Tasks: desktopicon

; Projects open with Harmonia: Explorer passes the file (Main.cpp, core::project_files).
; HKA is the machine's classes for everyone, the user's for "only for me".
[Registry]
Root: HKA; Subkey: "Software\Classes\.harmonia"; ValueType: string; ValueName: ""; ValueData: "Harmonia.Project"; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\.harmonia\OpenWithProgids"; ValueType: string; ValueName: "Harmonia.Project"; ValueData: ""; Flags: uninsdeletevalue uninsdeletekeyifempty; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Harmonia.Project"; ValueType: string; ValueName: ""; ValueData: "{cm:ProjectFile}"; Flags: uninsdeletekey; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Harmonia.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Harmonia.exe,0"; Tasks: associate
Root: HKA; Subkey: "Software\Classes\Harmonia.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Harmonia.exe"" ""%1"""; Tasks: associate

[Run]
Filename: "{app}\Harmonia.exe"; Description: "{cm:LaunchProgram,Harmonia}"; Flags: nowait postinstall skipifsilent
