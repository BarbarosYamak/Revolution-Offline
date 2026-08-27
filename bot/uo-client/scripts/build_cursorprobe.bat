@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /I "%~dp0..\include" /I "%~dp0..\src" "%~dp0..\tests\cursor_probe.cpp" "%~dp0..\src\mul\ArtLoader.cpp" "%~dp0..\src\mul\File.cpp" /Fe:"%~dp0..\build\cursor_probe.exe" /Fo:"%~dp0..\build\\"
if errorlevel 1 exit /b 1
"%~dp0..\build\cursor_probe.exe" %*
exit /b %errorlevel%
