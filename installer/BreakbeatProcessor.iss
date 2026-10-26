; ===========================================================================
;  Breakbeat Processor - Windows installer (Inno Setup 6.3 or newer)
;  build-windows.bat compiles this file when Inno Setup is installed.
; ===========================================================================

#define AppName      "Breakbeat Processor"
#define AppVersion   "1.0.0"
#define Root         AddBackslash(SourcePath) + "..\"
#define Dist64       Root + "dist\64-bit\"
#define Dist32       Root + "dist\32-bit\"

#if !DirExists(Dist64 + "Breakbeat Processor.vst3")
  #error "Breakbeat Processor.vst3 was not found in dist\64-bit. Run build-windows.bat first."
#endif
#define Has32 DirExists(Dist32 + "Breakbeat Processor.vst3")

[Setup]
AppId={{9B6E2F41-0C7D-4A55-8E21-5F3A7C1D2B90}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Four Tracker
AppPublisherURL=https://elcrispo76.github.io/Four-Tracker/
DefaultDirName={autopf}\Breakbeat Processor
DisableDirPage=yes
DisableProgramGroupPage=yes
OutputDir={#Root}dist
OutputBaseFilename=BreakbeatProcessor-Setup-{#AppVersion}
SetupIconFile={#Root}Resources\Breakbeat.ico
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
Source: "{#Dist64}Breakbeat Processor.vst3\*"; DestDir: "{commoncf64}\VST3\Breakbeat Processor.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#if Has32
Source: "{#Dist32}Breakbeat Processor.vst3\*"; DestDir: "{commoncf32}\VST3\Breakbeat Processor.vst3"; Components: vst3_32; Flags: ignoreversion recursesubdirs createallsubdirs
#endif

[Messages]
FinishedLabel=Breakbeat Processor is installed in Common Files\VST3. Rescan the plugins in your DAW, or in Four Tracker press FX (F8) and SCAN.
