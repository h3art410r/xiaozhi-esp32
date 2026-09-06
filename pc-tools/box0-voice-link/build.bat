@echo off
rem Build box0_voice_link.dll (MicYou native plugin).
rem Requires a MinGW-w64 g++ toolchain; set MINGW_DIR to its root (the dir containing bin\g++.exe).
if "%MINGW_DIR%"=="" set MINGW_DIR=D:\Opts\winlibs\mingw64
cd /d %~dp0
"%MINGW_DIR%\bin\g++.exe" -O2 -shared -static -o box0_voice_link.dll voice_link_plugin.cpp -lws2_32
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
echo Build OK: box0_voice_link.dll