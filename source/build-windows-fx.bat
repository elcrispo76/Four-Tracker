@echo off
rem ===========================================================================
rem  Four Tracker FX - personal build that can load VST effects
rem  (2 effects per channel, standalone app). Not for sharing.
rem  Output goes to dist-fx\64-bit and dist-fx\32-bit
rem ===========================================================================
call "%~dp0build-windows.bat" fx
