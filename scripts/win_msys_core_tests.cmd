@echo off
setlocal
if "%MSYS2_DIR%"=="" set "MSYS2_DIR=C:\msys64"

set "MSYSTEM=MINGW64"
set "CHERE_INVOKING=1"
set "PATH=%MSYS2_DIR%\mingw64\bin;%MSYS2_DIR%\usr\bin;%PATH%"

set "BASH=%MSYS2_DIR%\usr\bin\bash.exe"

rem Compile and run unit tests
"%BASH%" -lc "mkdir -p build && g++ -std=c++17 -O2 -Wall -DUNIT_TESTS src/core/Strum.cpp src/core/PolyQuantaCore.cpp src/core/ScaleDefs.cpp tests/main.cpp -Isrc -o build/core_tests && ./build/core_tests"
endlocal
