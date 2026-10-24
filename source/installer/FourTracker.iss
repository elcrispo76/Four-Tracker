; ===========================================================================
;  Four Tracker - Windows installer (Inno Setup 6.3 or newer)
;
;  Build the program first with build-windows.bat, then run
;  build-installer.bat (or open this file in Inno Setup and press Compile).
;  The finished installer is written to the dist folder.
; ===========================================================================

#define AppName      "Four Tracker"
#define AppVersion   "0.4.0"
#define AppPublisher "Four Tracker"
#define AppExe       "Four Tracker.exe"
#define Root         AddBackslash(SourcePath) + "..\"
; FX = the personal version with VST effect hosting (built by build-windows-fx.bat)
; FXSHARE = the FX version to share, VST3 effects only (built by build-windows-fx-share.bat)
#ifdef FXSHARE
  #define DistDir    "dist-fx-share"
#else
  #ifdef FX
    #define DistDir  "dist-fx"
  #else
    #define DistDir  "dist"
  #endif
#endif
#define Dist64       Root + DistDir + "\64-bit\"
#define Dist32       Root + DistDir + "\32-bit\"

#if !FileExists(Dist64 + AppExe)
  #error "Four Tracker.exe was not found in dist (or dist-fx for the FX version). Run build-windows.bat or build-windows-fx.bat first."
#endif

#define HasVst3_64  DirExists(Dist64 + "Four Tracker.vst3")
#define HasVst3_32  DirExists(Dist32 + "Four Tracker.vst3")
; SHARE = the version you give to others: never includes VST2 (Steinberg's
; VST2 licence does not allow new developers to distribute VST2 plugins).
#ifdef SHARE
  #define HasVst2_64  0
  #define HasVst2_32  0
  #ifdef FXSHARE
    #define Edition   "-share-fx"
  #else
    #define Edition   "-share"
  #endif
#else
  #define HasVst2_64  FileExists(Dist64 + "Four Tracker.dll")
  #define HasVst2_32  FileExists(Dist32 + "Four Tracker.dll")
  #ifdef FX
    #define Edition   "-personal-fx"
  #else
    #define Edition   "-personal"
  #endif
#endif
#define HasManual   FileExists(AddBackslash(SourcePath) + "Four Tracker User Manual.pdf")

[Setup]
AppId={{6E3B0C8A-4F1D-4B7E-9A55-2C7F31D9E4A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
OutputDir={#Root}{#DistDir}
OutputBaseFilename=FourTracker-Setup-{#AppVersion}{#Edition}
SetupIconFile={#Root}Resources\FourTracker.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
CloseApplications=yes

[Types]
Name: "full";   Description: "Program and all plugins"
Name: "app";    Description: "Program only"
Name: "custom"; Description: "Choose what to install"; Flags: iscustom

[Components]
Name: "app";     Description: "Four Tracker (standalone program)"; Types: full app custom; Flags: fixed
#if HasVst3_64
Name: "vst3";    Description: "VST3 plugin, 64-bit"; Types: full
#endif
#if HasVst3_32
Name: "vst3_32"; Description: "VST3 plugin, 32-bit (for 32-bit hosts)"; Types: full
#endif
#if HasVst2_64
Name: "vst2";    Description: "VST2 plugin, 64-bit"; Types: full
#endif
#if HasVst2_32
Name: "vst2_32"; Description: "VST2 plugin, 32-bit (for 32-bit hosts such as older Music Maker)"; Types: full
#endif

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "{#Dist64}{#AppExe}"; DestDir: "{app}"; Components: app; Flags: ignoreversion
#if HasManual
Source: "Four Tracker User Manual.pdf"; DestDir: "{app}"; Components: app; Flags: ignoreversion
#endif
#if HasVst3_64
Source: "{#Dist64}Four Tracker.vst3\*"; DestDir: "{commoncf64}\VST3\Four Tracker.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
#if HasVst3_32
Source: "{#Dist32}Four Tracker.vst3\*"; DestDir: "{commoncf32}\VST3\Four Tracker.vst3"; Components: vst3_32; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
#if HasVst2_64
Source: "{#Dist64}Four Tracker.dll"; DestDir: "{code:GetVst2Dir64}"; Components: vst2; Flags: ignoreversion
#endif
#if HasVst2_32
Source: "{#Dist32}Four Tracker.dll"; DestDir: "{code:GetVst2Dir32}"; Components: vst2_32; Flags: ignoreversion
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
#if HasManual
Name: "{group}\{#AppName} User Manual"; Filename: "{app}\Four Tracker User Manual.pdf"
#endif
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "Start {#AppName} now"; Flags: nowait postinstall skipifsilent

[Code]
var
  Vst2Page: TInputDirWizardPage;

function Vst2Selected(): Boolean;
begin
  Result := False;
#if HasVst2_64
  Result := Result or WizardIsComponentSelected('vst2');
#endif
#if HasVst2_32
  Result := Result or WizardIsComponentSelected('vst2_32');
#endif
end;

procedure InitializeWizard;
begin
  Vst2Page := CreateInputDirPage(wpSelectComponents,
    'VST2 plugin folders',
    'Where should the VST2 plugins be installed?',
    'VST2 plugins have no fixed folder. Choose the folder your music program scans for ' +
    'VST plugins (you can see it in the program''s plugin settings). ' +
    'A 32-bit program needs the 32-bit plugin.',
    False, '');
  Vst2Page.Add('64-bit VST2 folder:');
  Vst2Page.Add('32-bit VST2 folder:');
  Vst2Page.Values[0] := GetPreviousData('Vst2Dir64', ExpandConstant('{commonpf64}\Steinberg\VstPlugins'));
  Vst2Page.Values[1] := GetPreviousData('Vst2Dir32', ExpandConstant('{commonpf32}\Steinberg\VstPlugins'));
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = Vst2Page.ID) and not Vst2Selected();
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Vst2Dir64', Vst2Page.Values[0]);
  SetPreviousData(PreviousDataKey, 'Vst2Dir32', Vst2Page.Values[1]);
end;

function GetVst2Dir64(Param: String): String;
begin
  Result := Vst2Page.Values[0];
end;

function GetVst2Dir32(Param: String): String;
begin
  Result := Vst2Page.Values[1];
end;
