@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /I "%~dp0..\include" /I "%~dp0..\src" "%~dp0..\tests\path_probe.cpp" "%~dp0..\src\bot\Pathfinding.cpp" "%~dp0..\src\bot\Blacklist.cpp" "%~dp0..\src\mul\File.cpp" "%~dp0..\src\mul\TileDataLoader.cpp" "%~dp0..\src\mul\Map.cpp" "%~dp0..\src\mul\Verdata.cpp" "%~dp0..\src\mul\World.cpp" /Fe:"%~dp0..\build\path_probe.exe" /Fo:"%~dp0..\build\\"
if errorlevel 1 exit /b 1
"%~dp0..\build\path_probe.exe" %*
exit /b %errorlevel%
