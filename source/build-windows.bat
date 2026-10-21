@echo off
rem ===========================================================================
rem  Four Tracker - Windows build (64-bit and 32-bit)
rem
rem  Needs: Visual Studio 2022 or 2026 with "Desktop development with C++"
rem         and the Windows 11 SDK. Git is NOT needed.
rem
rem  Optional SDKs (put them here before running):
rem    SDKs\asiosdk\common\iasiodrv.h                 -> ASIO in the standalone app
rem    SDKs\vst2sdk\pluginterfaces\vst2.x\aeffect.h    -> VST2 plugin (.dll)
rem
rem  Output goes to dist\64-bit and dist\32-bit
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Four Tracker - build

echo.
echo  FOUR TRACKER - BUILD
echo  ====================
echo  Folder: %CD%
echo.

rem ---- are we running from inside the zip? --------------------------------
if exist "CMakeLists.txt" goto :haveSource
echo  [FEJL] CMakeLists.txt blev ikke fundet ved siden af dette script.
echo         Du har sandsynligvis aabnet filen inde i zip-filen.
echo         Hoejreklik paa FourTracker-source.zip, vaelg "Udpak alle",
echo         og koer build-windows.bat fra den udpakkede mappe.
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

rem ---- get JUCE (no git needed) ----------------------------------------------
if exist "JUCE\CMakeLists.txt" goto :haveJuce

rem A JUCE folder that only contains a JUCE-8.0.10 subfolder: move it up
if not exist "JUCE\JUCE-8.0.10\CMakeLists.txt" goto :noNestedJuce
echo  Retter JUCE-mappen (JUCE\JUCE-8.0.10 -^> JUCE) ...
move "JUCE\JUCE-8.0.10" "_juce_fix" >nul
rmdir /s /q "JUCE"
move "_juce_fix" "JUCE" >nul
if exist "JUCE\CMakeLists.txt" goto :haveJuce
:noNestedJuce

if exist "juce.zip" goto :haveJuceZip
echo  Henter JUCE 8.0.10 (ca. 100 MB, kun foerste gang) ...
where curl >nul 2>nul
if errorlevel 1 goto :psDownload
curl -L --fail --retry 3 -o "juce.zip" "https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip"
if not errorlevel 1 goto :haveJuceZip
:psDownload
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12; Invoke-WebRequest 'https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip' -OutFile 'juce.zip'"
if not exist "juce.zip" goto :juceFail
:haveJuceZip

echo  Pakker JUCE ud ...
if exist "_juce_tmp" rmdir /s /q "_juce_tmp"
mkdir "_juce_tmp"
tar -xf "juce.zip" -C "_juce_tmp"
if exist "_juce_tmp\JUCE-8.0.10\CMakeLists.txt" goto :juceUnpacked
echo  tar virkede ikke - proever PowerShell (det kan tage et par minutter) ...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive 'juce.zip' -DestinationPath '_juce_tmp' -Force"
if not exist "_juce_tmp\JUCE-8.0.10\CMakeLists.txt" goto :juceFail
:juceUnpacked
if exist "JUCE" rmdir /s /q "JUCE"
move "_juce_tmp\JUCE-8.0.10" "JUCE" >nul
rmdir /s /q "_juce_tmp"
del "juce.zip"
if exist "JUCE\CMakeLists.txt" goto :haveJuce

:juceFail
echo  [FEJL] Kunne ikke hente eller pakke JUCE ud.
echo         Tjek internetforbindelsen og proev igen. Eller goer det i haanden:
echo         1. Hent https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.10.zip
echo         2. Gem den som juce.zip i denne mappe
echo         3. Koer build-windows.bat igen - den pakker den selv ud
goto :fail
:haveJuce
echo  JUCE: OK
echo.

rem ---- optional SDKs ----------------------------------------------------------
rem  CMake searches the SDKs folder by itself (any sub-folder layout works) and
rem  prints ">>> VST2 SDK FUNDET" or ">>> VST2 SDK IKKE FUNDET" below.
set "EXTRA="
if not exist "SDKs" echo  Ingen SDKs-mappe - bygger Standalone + VST3 (uden VST2 og ASIO)
echo.

rem ---- build -----------------------------------------------------------------
call :build x64 64-bit
if errorlevel 1 goto :fail
call :build Win32 32-bit
if errorlevel 1 goto :fail32

echo.
echo  ===================================================================
echo   FAERDIG!  Appen ligger her:
echo     %CD%\dist\64-bit\Four Tracker.exe
echo  ===================================================================
echo.
echo  Bygget i dist\64-bit:
dir /b "dist\64-bit"
echo.
echo  Bygget i dist\32-bit:
dir /b "dist\32-bit"
if not exist "dist\64-bit\Four Tracker.dll" echo.
if not exist "dist\64-bit\Four Tracker.dll" echo  OBS: ingen VST2 (Four Tracker.dll) - se ">>> VST2 SDK" linjen laengere oppe.
start "" explorer "%CD%\dist\64-bit"
goto :end

:fail32
echo.
echo  64-bit versionen blev bygget og ligger i dist\64-bit,
echo  men 32-bit versionen fejlede (se beskeden ovenfor).
start "" explorer "%CD%\dist\64-bit"
goto :end

:fail
echo.
echo  ===================================================================
echo   BYGGET FEJLEDE. Kopier teksten i dette vindue og send den til Claude.
echo   (Marker med musen og tryk Enter for at kopiere.)
echo  ===================================================================
goto :end

:end
echo.
pause
exit /b

rem ---------------------------------------------------------------------------
:build
echo.
echo  ===== Bygger %2 (%1) =====
"%CMAKE%" -S . -B "build-%1" -A %1 %EXTRA%
if errorlevel 1 (
    echo.
    echo  CMake fejlede - rydder build-%1 og proever forfra ...
    rmdir /s /q "build-%1"
    "%CMAKE%" -S . -B "build-%1" -A %1 %EXTRA%
)
if errorlevel 1 exit /b 1
"%CMAKE%" --build "build-%1" --config Release --parallel
if errorlevel 1 exit /b 1

set "ART=build-%1\FourTracker_artefacts\Release"
set "OUT=dist\%2"
if not exist "%OUT%" mkdir "%OUT%"
copy /Y "%ART%\Standalone\Four Tracker.exe" "%OUT%\" >nul
if exist "%ART%\VST3\Four Tracker.vst3" xcopy /E /I /Y /Q "%ART%\VST3\Four Tracker.vst3" "%OUT%\Four Tracker.vst3" >nul
if exist "%ART%\VST\Four Tracker.dll" copy /Y "%ART%\VST\Four Tracker.dll" "%OUT%\" >nul
exit /b 0
