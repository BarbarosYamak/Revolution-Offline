@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "C:\Projects\RevolutionOffline\bot\uo-client"
cmake -S . -B build-fix11 -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build-fix11 -- -k 0
if errorlevel 1 exit /b 1
ctest --test-dir build-fix11 --output-on-failure
