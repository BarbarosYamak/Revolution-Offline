@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++20 /I "%~dp0..\include" /I "%~dp0..\src" "%~dp0..\tests\world_viewer.cpp" "%~dp0..\src\render\Renderer.cpp" "%~dp0..\src\mul\ArtLoader.cpp" "%~dp0..\src\mul\TexmapLoader.cpp" "%~dp0..\src\mul\AnimLoader.cpp" "%~dp0..\src\mul\TileDataLoader.cpp" "%~dp0..\src\mul\File.cpp" "%~dp0..\src\mul\Map.cpp" "%~dp0..\src\mul\Verdata.cpp" /Fe:"%~dp0..\build\world_viewer.exe" /Fo:"%~dp0..\build\\" /link user32.lib gdi32.lib
if errorlevel 1 exit /b 1
"%~dp0..\build\world_viewer.exe" %*
exit /b %errorlevel%
