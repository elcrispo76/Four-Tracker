; ===========================================================================
;  Four Tracker Effects Bundle - all the free effects in one installer
;  (Inno Setup 6.3 or newer). build-bundle.bat compiles this file.
;
;  It takes the VST3 builds that each plugin's own build-windows.bat made
;  (<plugin folder>\dist\64-bit and dist\32-bit). A plugin that hasn't been
;  built is simply left out of the bundle. VST3 only: never put VST2 builds here.
; ===========================================================================

#define AppName      "Four Tracker Effects Bundle"
#define AppVersion   "1.0.0"
#define Top          AddBackslash(SourcePath) + "..\..\"
#define Found        0
#define BB64 Top + "breakbeat-processor\dist\64-bit\Breakbeat Processor.vst3"
#define BB32 Top + "breakbeat-processor\dist\32-bit\Breakbeat Processor.vst3"
#define HasBB   DirExists(BB64)
#define HasBB32 DirExists(BB32) && DirExists(BB64)
#if HasBB
  #define Found Found + 1
#endif
#define FX164 Top + "fx-processor\dist\64-bit\FX Processor.vst3"
#define FX132 Top + "fx-processor\dist\32-bit\FX Processor.vst3"
#define HasFX1   DirExists(FX164)
#define HasFX132 DirExists(FX132) && DirExists(FX164)
#if HasFX1
  #define Found Found + 1
#endif
#define FX264 Top + "fx-processor-2\dist\64-bit\FX Processor 2.vst3"
#define FX232 Top + "fx-processor-2\dist\32-bit\FX Processor 2.vst3"
#define HasFX2   DirExists(FX264)
#define HasFX232 DirExists(FX232) && DirExists(FX264)
#if HasFX2
  #define Found Found + 1
#endif
#define DYN64 Top + "dynamics-processor\dist\64-bit\Dynamics Processor.vst3"
#define DYN32 Top + "dynamics-processor\dist\32-bit\Dynamics Processor.vst3"
#define HasDYN   DirExists(DYN64)
#define HasDYN32 DirExists(DYN32) && DirExists(DYN64)
#if HasDYN
  #define Found Found + 1
#endif
#define EQ64 Top + "graphic-eq\dist\64-bit\Graphic EQ.vst3"
#define EQ32 Top + "graphic-eq\dist\32-bit\Graphic EQ.vst3"
#define HasEQ   DirExists(EQ64)
#define HasEQ32 DirExists(EQ32) && DirExists(EQ64)
#if HasEQ
  #define Found Found + 1
#endif
#if Found == 0
  #error "No effects found. Run build-windows.bat in each plugin folder first (dist\64-bit must hold the .vst3)."
#endif

[Setup]
AppId={{8E5F2A91-7C34-4D0B-B6E8-3A1F9C2D7E50}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Four Tracker
AppPublisherURL=https://elcrispo76.github.io/Four-Tracker/
DefaultDirName={autopf}\Four Tracker Effects Bundle
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#Top}effects-bundle\dist
OutputBaseFilename=FourTrackerEffects-Bundle-{#AppVersion}
SetupIconFile={#Top}effects-bundle\installer\Bundle.ico
UninstallDisplayIcon={uninstallexe}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin

[Types]
Name: "full";   Description: "All effects, 64-bit and 32-bit"
Name: "only64"; Description: "All effects, 64-bit only"
Name: "custom"; Description: "Choose"; Flags: iscustom

[Components]
#if HasBB
Name: "bb";     Description: "Breakbeat Processor - 8 step breakbeat slicer in time with the song"; Types: full only64 custom
Name: "bb\x64"; Description: "64-bit VST3"; Types: full only64 custom
  #if HasBB32
Name: "bb\x86"; Description: "32-bit VST3 (for 32-bit hosts)"; Types: full
  #endif
#endif
#if HasFX1
Name: "fx1";     Description: "FX Processor - filter, PW gate, chorus, LFO, delay, reverb"; Types: full only64 custom
Name: "fx1\x64"; Description: "64-bit VST3"; Types: full only64 custom
  #if HasFX132
Name: "fx1\x86"; Description: "32-bit VST3 (for 32-bit hosts)"; Types: full
  #endif
#endif
#if HasFX2
Name: "fx2";     Description: "FX Processor 2 - phaser, flanger, ring modulator"; Types: full only64 custom
Name: "fx2\x64"; Description: "64-bit VST3"; Types: full only64 custom
  #if HasFX232
Name: "fx2\x86"; Description: "32-bit VST3 (for 32-bit hosts)"; Types: full
  #endif
#endif
#if HasDYN
Name: "dyn";     Description: "Dynamics Processor - transient shaper, compressor, saturation, AUTO"; Types: full only64 custom
Name: "dyn\x64"; Description: "64-bit VST3"; Types: full only64 custom
  #if HasDYN32
Name: "dyn\x86"; Description: "32-bit VST3 (for 32-bit hosts)"; Types: full
  #endif
#endif
#if HasEQ
Name: "eq";     Description: "Graphic EQ - 10 band graphic EQ"; Types: full only64 custom
Name: "eq\x64"; Description: "64-bit VST3"; Types: full only64 custom
  #if HasEQ32
Name: "eq\x86"; Description: "32-bit VST3 (for 32-bit hosts)"; Types: full
  #endif
#endif

[Files]
#if HasBB
Source: "{#BB64}\*"; DestDir: "{code:GetVst3Dir64}\Breakbeat Processor.vst3"; Components: bb\x64; Flags: ignoreversion recursesubdirs createallsubdirs
  #if HasBB32
Source: "{#BB32}\*"; DestDir: "{code:GetVst3Dir32}\Breakbeat Processor.vst3"; Components: bb\x86; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif
#if HasFX1
Source: "{#FX164}\*"; DestDir: "{code:GetVst3Dir64}\FX Processor.vst3"; Components: fx1\x64; Flags: ignoreversion recursesubdirs createallsubdirs
  #if HasFX132
Source: "{#FX132}\*"; DestDir: "{code:GetVst3Dir32}\FX Processor.vst3"; Components: fx1\x86; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif
#if HasFX2
Source: "{#FX264}\*"; DestDir: "{code:GetVst3Dir64}\FX Processor 2.vst3"; Components: fx2\x64; Flags: ignoreversion recursesubdirs createallsubdirs
  #if HasFX232
Source: "{#FX232}\*"; DestDir: "{code:GetVst3Dir32}\FX Processor 2.vst3"; Components: fx2\x86; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif
#if HasDYN
Source: "{#DYN64}\*"; DestDir: "{code:GetVst3Dir64}\Dynamics Processor.vst3"; Components: dyn\x64; Flags: ignoreversion recursesubdirs createallsubdirs
  #if HasDYN32
Source: "{#DYN32}\*"; DestDir: "{code:GetVst3Dir32}\Dynamics Processor.vst3"; Components: dyn\x86; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif
#if HasEQ
Source: "{#EQ64}\*"; DestDir: "{code:GetVst3Dir64}\Graphic EQ.vst3"; Components: eq\x64; Flags: ignoreversion recursesubdirs createallsubdirs
  #if HasEQ32
Source: "{#EQ32}\*"; DestDir: "{code:GetVst3Dir32}\Graphic EQ.vst3"; Components: eq\x86; Flags: ignoreversion recursesubdirs createallsubdirs
  #endif
#endif

[Messages]
FinishedLabel=The effects are installed in the VST3 folder you chose. Rescan the plugins in your DAW, or in Four Tracker press FX (F8) and SCAN.

[Code]
var
  Vst3Page: TInputDirWizardPage;

procedure InitializeWizard;
begin
  Vst3Page := CreateInputDirPage(wpSelectComponents,
    'VST3 folders',
    'Where should the VST3 plugins be installed?',
    'The standard VST3 folder (Common Files\VST3) is found by every DAW and by Four Tracker. ' +
    'Choose another folder if your music program scans its own plugin folder. ' +
    'A 32-bit program needs the 32-bit plugins.',
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
