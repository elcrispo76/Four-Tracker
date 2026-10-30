; ===========================================================================
;  Graphic EQ - Windows installer (Inno Setup 6.3 or newer)
;  build-windows.bat compiles this file when Inno Setup is installed.
; ===========================================================================

#define AppName      "Graphic EQ"
#define AppVersion   "1.0.0"
#define Root         AddBackslash(SourcePath) + "..\"
#define Dist64       Root + "dist\64-bit\"
#define Dist32       Root + "dist\32-bit\"

#if !DirExists(Dist64 + "Graphic EQ.vst3")
  #error "Graphic EQ.vst3 was not found in dist\64-bit. Run build-windows.bat first."
#endif
#define Has32 DirExists(Dist32 + "Graphic EQ.vst3")

[Setup]
AppId={{A2C7E915-6D3B-4F48-9E07-1B5D8C3F6A72}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Four Tracker
AppPublisherURL=https://elcrispo76.github.io/Four-Tracker/
DefaultDirName={autopf}\Graphic EQ
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#Root}dist
OutputBaseFilename=GraphicEq-Setup-{#AppVersion}
SetupIconFile={#Root}Resources\GraphicEq.ico
UninstallDisplayIcon={uninstallexe}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Types]
Name: "full";   Description: "64-bit and 32-bit"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
Name: "vst3";     Description: "VST3 plugin, 64-bit"; Types: full custom
#if Has32
Name: "vst3_32";  Description: "VST3 plugin, 32-bit (for 32-bit hosts)"; Types: full
#endif

[Files]
Source: "{#Dist64}Graphic EQ.vst3\*"; DestDir: "{code:GetVst3Dir64}\Graphic EQ.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#if Has32
Source: "{#Dist32}Graphic EQ.vst3\*"; DestDir: "{code:GetVst3Dir32}\Graphic EQ.vst3"; Components: vst3_32; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Messages]
FinishedLabel=Graphic EQ is installed in the VST3 folder you chose. Rescan the plugins in your DAW, or in Four Tracker press FX (F8) and SCAN.

[Code]
var
  Vst3Page: TInputDirWizardPage;

procedure InitializeWizard;
begin
  Vst3Page := CreateInputDirPage(wpSelectComponents,
    'VST3 folders',
    'Where should the VST3 plugin be installed?',
    'The standard VST3 folder (Common Files\VST3) is found by every DAW and by Four Tracker. ' +
    'Choose another folder if your music program scans its own plugin folder. ' +
    'A 32-bit program needs the 32-bit plugin.',
    False, '');
  Vst3Page.Add('64-bit VST3 folder:');
  Vst3Page.Add('32-bit VST3 folder:');
  Vst3Page.Values[0] := GetPreviousData('Vst3Dir64', ExpandConstant('{commoncf64}\VST3'));
  Vst3Page.Values[1] := GetPreviousData('Vst3Dir32', ExpandConstant('{commoncf32}\VST3'));
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'Vst3Dir64', Vst3Page.Values[0]);
  SetPreviousData(PreviousDataKey, 'Vst3Dir32', Vst3Page.Values[1]);
end;

function GetVst3Dir64(Param: String): String;
begin
  Result := Vst3Page.Values[0];
end;

function GetVst3Dir32(Param: String): String;
begin
  Result := Vst3Page.Values[1];
end;
