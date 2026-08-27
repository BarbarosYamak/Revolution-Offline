@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cmake --build "%~dp0..\build" --target uo_mul > "%~dp0..\build\vchk.txt" 2>&1
