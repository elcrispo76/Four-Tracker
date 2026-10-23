@echo off
rem ===========================================================================
rem  Four Tracker - builds the installer
rem
rem  Needs: Inno Setup 6.3+ (6.7.3 recommended) or 7 (free): https://jrsoftware.org/isdl.php
rem  1 / 2: run build-windows.bat first (the program goes in dist)
rem  3    : run build-windows-fx.bat first (the FX version goes in dist-fx)
rem  4    : run build-windows-fx-share.bat first (the FX version to share goes in dist-fx-share)
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Four Tracker - installer

echo.
echo  FOUR TRACKER - INSTALLER
echo  ========================
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
echo         Hent og installer Inno Setup 6.7.3 (gratis) fra https://jrsoftware.org/isdl.php
echo         og koer build-installer.bat igen.
goto :fail
:haveIscc
echo  Inno Setup: %ISCC%
echo.
echo  Hvilken installationsfil vil du lave?
echo    1 = Til dig selv   (med VST2, hvis den er bygget)       - fra mappen dist
echo    2 = Til deling     (uden VST2 - kun program og VST3)    - fra mappen dist
echo    3 = FX-version     (til dig selv, VST3 + VST2 effekter)    - fra mappen dist-fx
echo    4 = FX til deling  (VST3 effekter, ingen VST2)             - fra mappen dist-fx-share
echo.
choice /c 1234 /n /m "  Tryk 1, 2, 3 eller 4: "
set "PICK=%errorlevel%"
echo.

set "EDITION="
set "DISTDIR=dist"
set "BUILDER=build-windows.bat"
if "%PICK%"=="2" set "EDITION=/DSHARE"
if "%PICK%"=="3" set "EDITION=/DFX"
if "%PICK%"=="3" set "DISTDIR=dist-fx"
if "%PICK%"=="3" set "BUILDER=build-windows-fx.bat"
if "%PICK%"=="4" set "EDITION=/DSHARE /DFXSHARE"
if "%PICK%"=="4" set "DISTDIR=dist-fx-share"
if "%PICK%"=="4" set "BUILDER=build-windows-fx-share.bat"

if exist "%DISTDIR%\64-bit\Four Tracker.exe" goto :haveDist
echo  [FEJL] %DISTDIR%\64-bit\Four Tracker.exe findes ikke.
echo         Koer %BUILDER% foerst.
goto :fail
:haveDist

"%ISCC%" %EDITION% "installer\FourTracker.iss"
if errorlevel 1 goto :fail

echo.
echo  ===================================================================
echo   FAERDIG!  Installeren ligger i mappen %DISTDIR%:
dir /b "%DISTDIR%\FourTracker-Setup-*.exe"
echo  ===================================================================
start "" explorer "%CD%\%DISTDIR%"
goto :end

:fail
echo.
echo  ===================================================================
echo   INSTALLEREN BLEV IKKE LAVET. Send teksten i dette vindue til Claude.
echo  ===================================================================

:end
echo.
pause
