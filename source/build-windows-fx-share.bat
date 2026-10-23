@echo off
rem ===========================================================================
rem  Four Tracker FX - the version to SHARE that can load VST3 effects
rem  (2 effects per channel + chaining, standalone app). No VST2 at all,
rem  even if the VST2 SDK is in SDKs. VST3 is MIT licensed, so this is fine to share.
rem  Output goes to dist-fx-share\64-bit and dist-fx-share\32-bit
rem ===========================================================================
call "%~dp0build-windows.bat" fxshare
