@echo off
rem ===========================================================================
rem  Four Tracker Effects Bundle - one installer with all the free effects
rem
rem  First run build-windows.bat in each plugin folder (the VST3 share builds),
rem  then this. Plugins that haven't been built are left out.
rem  Output: effects-bundle\dist\FourTrackerEffects-Bundle-1.0.0.exe
rem  Needs Inno Setup 6.3 or newer. VST3 only - never VST2.
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Four Tracker Effects Bundle - installer

echo.
echo  FOUR TRACKER EFFECTS BUNDLE
echo  ===========================
echo  Disse effekter er bygget og kommer med:
set "ANY="
for %%P in ("breakbeat-processor|Breakbeat Processor" "fx-processor|FX Processor" "fx-processor-2|FX Processor 2" "dynamics-processor|Dynamics Processor" "graphic-eq|Graphic EQ") do call :check %%P
if defined ANY goto :haveAny
echo.
echo  [FEJL] Ingen effekter er bygget endnu.
echo         Koer build-windows.bat i hver plugin-mappe foerst.
goto :end
:haveAny
echo.

set "ISCC="
for %%V in (6 7) do (
    if exist "%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup %%V\ISCC.exe"
    if exist "%ProgramFiles%\Inno Setup %%V\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup %%V\ISCC.exe"
    if exist "%LOCALAPPDATA%\Programs\Inno Setup %%V\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup %%V\ISCC.exe"
)
if defined ISCC goto :haveIscc
where iscc >nul 2>nul
if errorlevel 1 goto :noIscc
set "ISCC=iscc"
goto :haveIscc
:noIscc
echo  [FEJL] Inno Setup blev ikke fundet.
echo         Hent og installer Inno Setup (gratis) fra https://jrsoftware.org/isdl.php
echo         Eller aabn installer\EffectsBundle.iss i Inno Setup og tryk Compile.
goto :end
:haveIscc
echo  Inno Setup: %ISCC%
"%ISCC%" "installer\EffectsBundle.iss"
if errorlevel 1 (
    echo.
    echo  INSTALLEREN FEJLEDE. Kopier teksten i dette vindue og send den til Claude.
    goto :end
)
echo.
echo  FAERDIG: dist\FourTrackerEffects-Bundle-1.0.0.exe
start "" explorer "%CD%\dist"
goto :end

:check
for /f "tokens=1,2 delims=|" %%A in (%1) do (
    if exist "..\%%A\dist\64-bit\%%B.vst3" (
        set "ANY=1"
        if exist "..\%%A\dist\32-bit\%%B.vst3" (echo    [X] %%B   64-bit + 32-bit) else (echo    [X] %%B   64-bit)
    ) else (
        echo    [ ] %%B   ikke bygget - koer build-windows.bat i %%A
    )
)
exit /b 0

:end
echo.
pause
