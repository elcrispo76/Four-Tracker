@echo off
rem ===========================================================================
rem  Four Tracker - builds the installer (dist\FourTracker-Setup-x.y.z.exe)
rem
rem  Needs: Inno Setup 6.3+ (6.7.3 recommended) or 7 (free): https://jrsoftware.org/isdl.php
rem  Run build-windows.bat first, so the dist folder holds the program.
rem ===========================================================================
setlocal EnableExtensions
cd /d "%~dp0"
title Four Tracker - installer

echo.
echo  FOUR TRACKER - INSTALLER
echo  ========================
echo.

if exist "dist\64-bit\Four Tracker.exe" goto :haveDist
echo  [FEJL] dist\64-bit\Four Tracker.exe findes ikke.
echo         Koer build-windows.bat foerst.
goto :fail
:haveDist

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
echo    1 = Til dig selv  (med VST2, hvis den er bygget)
echo    2 = Til deling    (uden VST2 - kun program og VST3)
echo.
choice /c 12 /n /m "  Tryk 1 eller 2: "
if errorlevel 2 (set "EDITION=/DSHARE") else (set "EDITION=")
echo.

"%ISCC%" %EDITION% "installer\FourTracker.iss"
if errorlevel 1 goto :fail

echo.
echo  ===================================================================
echo   FAERDIG!  Installeren ligger i mappen dist:
dir /b "dist\FourTracker-Setup-*.exe"
echo  ===================================================================
start "" explorer "%CD%\dist"
goto :end

:fail
echo.
echo  ===================================================================
echo   INSTALLEREN BLEV IKKE LAVET. Send teksten i dette vindue til Claude.
echo  ===================================================================

:end
echo.
pause
