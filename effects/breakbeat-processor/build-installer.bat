@echo off
rem ===========================================================================
rem  Breakbeat Processor - makes the installer (needs Inno Setup 6.3 or newer)
rem  Run build-windows.bat first (dist\64-bit and dist\32-bit must hold the VST3).
rem  Output: dist\BreakbeatProcessor-Setup-<version>.exe
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Breakbeat Processor - installer

if exist "dist\64-bit\Breakbeat Processor.vst3" goto :haveBuild
echo  [FEJL] dist\64-bit\Breakbeat Processor.vst3 mangler. Koer build-windows.bat foerst.
goto :end
:haveBuild

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
echo         Eller aabn installer\BreakbeatProcessor.iss i Inno Setup og tryk Compile.
goto :end
:haveIscc
echo  Inno Setup: %ISCC%
"%ISCC%" "installer\BreakbeatProcessor.iss"
if errorlevel 1 (
    echo.
    echo  INSTALLEREN FEJLEDE. Kopier teksten i dette vindue og send den til Claude.
    goto :end
)
echo.
echo  FAERDIG: dist\BreakbeatProcessor-Setup-1.0.0.exe
start "" explorer "%CD%\dist"

:end
echo.
pause
