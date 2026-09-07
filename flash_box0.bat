@echo off
rem ============================================================
rem  BOX0 one-click build + flash (offline-friendly)
rem
rem  Usage:  double-click flash_box0.bat
rem
rem  Optional environment variables:
rem    ESP_IDF_DIR  ESP-IDF path      (default D:\Opts\esp-idf-v6.0.2)
rem    BOX0_PORT    serial port       (default COM3)
rem
rem  WiFi credentials come from box0_config.ini (edit that file).
rem  NOTE: the first build on a fresh PC needs network access once to
rem  download managed_components; after that everything works offline.
rem ============================================================
setlocal
set "MSYSTEM="
set "MSYS2_PATH_TYPE="
if "%ESP_IDF_DIR%"=="" (
    if exist "C:\Workspace\esp-idf-v6.0.2\export.bat" (set ESP_IDF_DIR=C:\Workspace\esp-idf-v6.0.2) else (set ESP_IDF_DIR=D:\Opts\esp-idf-v6.0.2)
)
if "%BOX0_PORT%"=="" set BOX0_PORT=COM3
cd /d %~dp0

if not exist "%ESP_IDF_DIR%\export.bat" (
    echo [ERROR] ESP-IDF not found at "%ESP_IDF_DIR%".
    echo         Set ESP_IDF_DIR to your ESP-IDF v6.0.2 directory first.
    exit /b 1
)
call "%ESP_IDF_DIR%\export.bat" >nul
if errorlevel 1 (echo [ERROR] ESP-IDF environment setup failed & exit /b 1)

echo [1/3] Generating box0_local_config.h from box0_config.ini ...
python scripts\box0_gen_config.py
if errorlevel 1 exit /b 1

echo [2/3] Building firmware (alientek/atk-dnesp32s3-box0) ...
python scripts\build.py alientek/atk-dnesp32s3-box0 --name atk-dnesp32s3-box0
if errorlevel 1 (echo [ERROR] Build failed & exit /b 1)

echo [3/3] Flashing to %BOX0_PORT% ...
idf.py -p %BOX0_PORT% flash
if errorlevel 1 (echo [ERROR] Flash failed - is the BOX0 connected to %BOX0_PORT%? & exit /b 1)

echo.
echo === BOX0 flashed successfully ===
