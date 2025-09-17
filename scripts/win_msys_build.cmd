@echo off
setlocal
rem --- Configure MSYS2 location (override by setting MSYS2_DIR) ---
if "%MSYS2_DIR%"=="" set "MSYS2_DIR=C:\msys64"

set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
set "PATH=%MSYS2_DIR%\mingw64\bin;%MSYS2_DIR%\usr\bin;%PATH%"

rem Set Rack SDK environment variables
set "RACK_DIR=C:/dev/Rack-SDK"
set "RACK_SDK=C:/dev/Rack-SDK"
set "RACK_USER_DIR=C:/Users/Christian/AppData/Local/Rack2"
set "RACK_EXE=C:/Program Files/VCV/Rack2Free/Rack.exe"

set "BASH=%MSYS2_DIR%\usr\bin\bash.exe"

rem Build via MSYS2 bash
"%BASH%" -lc "make -j$(nproc)"
endlocal
