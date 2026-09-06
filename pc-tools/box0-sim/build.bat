@echo off
rem Build box0_sim.exe (LVGL SDL2 simulator for the BOX0 games).
rem Requires:
rem   - MinGW-w64 g++ (MINGW_DIR, default D:\Opts\winlibs\mingw64)
rem   - SDL2 mingw devel package extracted somewhere (SDL2_DIR = the folder that
rem     contains x86_64-w64-mingw32\, e.g. D:\Workspace\box0-sim\SDL2-2.32.10)
rem     Download: https://github.com/libsdl-org/SDL/releases (SDL2-devel-*-mingw.zip)
rem LVGL and the game code are taken from this repo (managed_components + main/boards).
setlocal
if "%MINGW_DIR%"=="" set MINGW_DIR=D:\Opts\winlibs\mingw64
if "%SDL2_DIR%"=="" set SDL2_DIR=D:\Workspace\box0-sim\SDL2-2.32.10
set MINGW=%MINGW_DIR%\bin
set SDL=%SDL2_DIR%\x86_64-w64-mingw32
set REPO=%~dp0..\..
set LVGL=%REPO%\managed_components\lvgl__lvgl
set GAMES=%REPO%\main\boards\alientek\atk-dnesp32s3-box0
cd /d %~dp0
python -c "import os,glob;fs=[p.replace(os.sep,'/') for p in glob.glob(r'%LVGL%/src/**/*.c',recursive=True) if 'vg_lite' not in p and 'nema' not in p];open('lvgl_sources.txt','w').write('\n'.join(fs))"
if not exist obj mkdir obj
cd obj
"%MINGW%\gcc.exe" -O2 -c -I.. -I"%LVGL%" -I"%LVGL%\src" -I"%SDL%\include" -DLV_CONF_PATH=\"%~dp0lv_conf.h\" @..\lvgl_sources.txt
if errorlevel 1 (echo LVGL BUILD FAILED & exit /b 1)
cd ..
python -c "import os,glob;open('obj_files.txt','w').write('\n'.join(p.replace(os.sep,'/') for p in glob.glob('obj/*.o')))"
"%MINGW%\g++.exe" -O2 -std=c++17 -I. -I"%LVGL%" -I"%LVGL%\src" -I"%GAMES%" -I"%SDL%\include" -DLV_CONF_PATH=\"%~dp0lv_conf.h\" main.cpp @obj_files.txt -L"%SDL%\lib" -lSDL2 -o box0_sim.exe
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
copy /y "%SDL%\bin\SDL2.dll" . >nul
echo Build OK: box0_sim.exe