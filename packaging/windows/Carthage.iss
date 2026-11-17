; Inno Setup script for Carthage on Windows x64 (docs/RELEASING.md). The release workflow passes
; the version and the folder that holds the built app:
;
;   iscc /DVersion=1.2.3-beta.4 /DNumericVersion=1.2.3 /DAppDir=<folder with Carthage.exe> /DOutDir=<out> Carthage.iss
;
; WinSparkle runs the same installer for updates, silently closing Carthage first.

#ifndef Version
  #define Version "0.0.0-dev"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef AppDir
  #error AppDir must point at the folder with Carthage.exe
#endif
#ifndef OutDir
  #define OutDir "."
#endif

[Setup]
AppId={{6E2B6E3A-3B7C-4C1E-9C3B-2E8F0C7A4D51}
AppName=Carthage
AppVersion={#Version}
AppVerName=Carthage {#Version}
AppPublisher=Jonne Nieminen
AppPublisherURL=https://github.com/JonneNieminen/Carthage
AppUpdatesURL=https://github.com/JonneNieminen/Carthage/releases
VersionInfoVersion={#NumericVersion}.0
DefaultDirName={autopf}\Carthage
DefaultGroupName=Carthage
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename=Carthage-{#Version}-x64-setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 version 1607 or later (DECISIONS W8)
MinVersion=10.0.14393
PrivilegesRequiredOverridesAllowed=dialog
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=yes
UninstallDisplayIcon={app}\Carthage.exe
LicenseFile=..\..\LICENSE

[Files]
Source: "{#AppDir}\Carthage.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppDir}\carthage-scan.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#AppDir}\WinSparkle.dll"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Carthage"; Filename: "{app}\Carthage.exe"

[Run]
Filename: "{app}\Carthage.exe"; Description: "Open Carthage"; Flags: nowait postinstall skipifsilent
