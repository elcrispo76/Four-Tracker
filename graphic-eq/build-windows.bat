@echo off
rem ===========================================================================
rem  Graphic EQ - Windows build (VST3, 64-bit and 32-bit)
rem
rem  Needs: Visual Studio 2022 or 2026 with "Desktop development with C++"
rem         and the Windows 11 SDK, and the JUCE folder (copy it from Four Tracker).
rem
rem  Output:  dist\64-bit\Graphic EQ.vst3
rem           dist\32-bit\Graphic EQ.vst3
rem  With Inno Setup installed the installer is made too:
rem           dist\GraphicEq-Setup-<version>.exe
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Graphic EQ - build

rem  "build-windows.bat vst2" (or build-windows-vst2.bat): also a VST2 .dll, for YOUR OWN USE
rem  only (VST2 SDK licence). Needs the VST2 SDK under SDKs. Output in dist-vst2, no installer.
set "EXTRA="
set "SUFFIX="
set "DISTDIR=dist"
if /i "%~1"=="vst2" (
    set "EXTRA=-DGEQ_VST2=ON"
    set "SUFFIX=-vst2"
    set "DISTDIR=dist-vst2"
    echo  *** VST2-VERSION: kun til dig selv - maa IKKE deles ***
)

echo.
echo  GRAPHIC EQ - BUILD
echo  ===========================
echo  Folder: %CD%
echo.

if exist "CMakeLists.txt" goto :haveSource
echo  [FEJL] CMakeLists.txt blev ikke fundet ved siden af dette script.
echo         Pak zip-filen ud foerst og koer build-windows.bat fra den udpakkede mappe.
goto :fail
:haveSource

rem ---- find Visual Studio and its CMake -------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto :haveVswhere
echo  [FEJL] Visual Studio Installer blev ikke fundet.
echo         Installer Visual Studio 2022 eller 2026 Community med
echo         "Desktop development with C++".
goto :fail
:haveVswhere

set "VSPATH="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH goto :haveVS
echo  [FEJL] Visual Studio er installeret, men uden C++ vaerktoejerne.
echo         Aabn "Visual Studio Installer", klik "Rediger" (Modify), og saet
echo         flueben ved "Desktop development with C++" og "Windows 11 SDK".
goto :fail
:haveVS
echo  Visual Studio: %VSPATH%

set "CMAKE=%VSPATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if exist "%CMAKE%" goto :haveCMake
where cmake >nul 2>nul
if errorlevel 1 goto :noCMake
set "CMAKE=cmake"
goto :haveCMake
:noCMake
echo  [FEJL] CMake blev ikke fundet.
echo         Aabn "Visual Studio Installer", klik "Rediger" (Modify), gaa til
echo         "Individual components" og saet flueben ved
echo         "C++ CMake tools for Windows".
goto :fail
:haveCMake
echo  CMake: %CMAKE%
"%CMAKE%" --version | findstr /i "version"
echo.

rem ---- JUCE ------------------------------------------------------------------
rem  This script downloads nothing. Copy the JUCE folder from your Four Tracker
rem  folder (the one build-windows.bat made there) into this folder.
if exist "JUCE\CMakeLists.txt" goto :haveJuce
if exist "..\JUCE\CMakeLists.txt" goto :haveJuce
if exist "JUCE\JUCE-8.0.10\CMakeLists.txt" (
    move "JUCE\JUCE-8.0.10" "_juce_fix" >nul
    rmdir /s /q "JUCE"
    move "_juce_fix" "JUCE" >nul
)
if exist "JUCE\CMakeLists.txt" goto :haveJuce
echo  [FEJL] JUCE-mappen mangler.
echo         Kopier mappen JUCE fra din Four Tracker-mappe (der hvor du har koert
echo         Four Trackers build-windows.bat) ind i denne mappe, saa der ligger
echo         JUCE\CMakeLists.txt her. Koer derefter build-windows.bat igen.
echo         (Eller hent https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip,
echo          pak den ud og omdoeb mappen JUCE-8.0.10 til JUCE.)
goto :fail
:haveJuce
echo  JUCE: OK
echo.

call :build x64 64-bit
if errorlevel 1 goto :fail
call :build Win32 32-bit
if errorlevel 1 goto :fail32
if /i "%DISTDIR%"=="dist-vst2" goto :doneVst2

rem ---- installer (when Inno Setup is installed) ------------------------------
set "ISCC="
for %%V in (6 7) do (
    if exist "%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe"
    if exist "%ProgramFiles%\Inno Setup %%V\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup %%V\ISCC.exe"
    if exist "%LOCALAPPDATA%\Programs\Inno Setup %%V\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup %%V\ISCC.exe"
)
if not defined ISCC goto :noInno
echo.
echo  ===== Laver installeren =====
"%ISCC%" "installer\GraphicEq.iss"
if errorlevel 1 goto :fail
goto :doneVst2
echo.
echo  ===================================================================
echo   FAERDIG (VST2, kun til dig selv):
echo     dist-vst2\32-bit\Graphic EQ.dll   (til 32-bit programmer som MadTracker)
echo     dist-vst2\64-bit\Graphic EQ.dll
echo   Kopier den .dll, der passer, til dit programs VST-mappe og scan igen.
echo  ===================================================================
start "" explorer "%CD%\dist-vst2"
goto :end

:done
:noInno
echo.
echo  Inno Setup blev ikke fundet - ingen installer (hent den gratis paa https://jrsoftware.org/isdl.php).

:done
echo.
echo  ===================================================================
echo   FAERDIG!
echo     dist\64-bit\Graphic EQ.vst3
echo     dist\32-bit\Graphic EQ.vst3
if defined ISCC echo     dist\GraphicEq-Setup-*.exe   (den du deler)
echo  ===================================================================
echo.
echo  Proev uden installer: kopier "Graphic EQ.vst3" (64-bit) til
echo    C:\Program Files\Common Files\VST3
echo  og scan igen i Four Tracker (FX, SCAN) eller i dit DAW.
start "" explorer "%CD%\dist"
goto :end

:fail32
echo.
echo  64-bit versionen blev bygget (%DISTDIR%\64-bit), men 32-bit fejlede (se ovenfor).
start "" explorer "%CD%\%DISTDIR%"
goto :end

:fail
echo.
echo  ===================================================================
echo   BYGGET FEJLEDE. Kopier teksten i dette vindue og send den til Claude.
echo  ===================================================================

:end
echo.
pause
exit /b

rem ---------------------------------------------------------------------------
:build
echo.
echo  ===== Bygger %2 (%1) =====
set "BDIR=build-%1%SUFFIX%"
"%CMAKE%" -S . -B "%BDIR%" -A %1 %EXTRA%
if errorlevel 1 (
    rmdir /s /q "%BDIR%"
    "%CMAKE%" -S . -B "%BDIR%" -A %1 %EXTRA%
)
if errorlevel 1 exit /b 1
"%CMAKE%" --build "%BDIR%" --config Release --parallel
if errorlevel 1 exit /b 1
set "OUT=%DISTDIR%\%2"
if not exist "%OUT%" mkdir "%OUT%"
xcopy /E /I /Y /Q "%BDIR%\GraphicEq_artefacts\Release\VST3\Graphic EQ.vst3" "%OUT%\Graphic EQ.vst3" >nul
if not exist "%OUT%\Graphic EQ.vst3" exit /b 1
if exist "%BDIR%\GraphicEq_artefacts\Release\VST\Graphic EQ.dll" copy /Y "%BDIR%\GraphicEq_artefacts\Release\VST\Graphic EQ.dll" "%OUT%\" >nul
exit /b 0
