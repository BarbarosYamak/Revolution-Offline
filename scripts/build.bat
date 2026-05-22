@echo off
setlocal
tasklist | findstr uo-client
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cmake -G Ninja -S "%~dp0.." -B "%~dp0..\build"
if errorlevel 1 exit /b 1
cmake --build "%~dp0..\build"
exit /b %errorlevel%
