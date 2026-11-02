@echo off
rem ===========================================================================
rem  FX Processor 2 - VST2 for YOUR OWN USE (e.g. MadTracker). Never share it:
rem  the VST2 SDK licence does not allow new VST2 plugins to be distributed.
rem  Needs the VST2 SDK under SDKs (copy the SDKs folder from Four Tracker).
rem  Output: dist-vst2\32-bit and dist-vst2\64-bit
rem ===========================================================================
call "%~dp0build-windows.bat" vst2
