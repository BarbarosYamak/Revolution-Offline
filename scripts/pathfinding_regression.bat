@echo off
setlocal
rem Pathfinding regression harness.
rem
rem These routes cover two separate multi-Z failures:
rem   01 down:    reach the negative-Z interior from the z40 floor.
rem   02 up:      leave the negative-Z interior and climb back to z40.
rem
rem Run after any change touching src/bot/Pathfinding.* or src/mul/World.cpp.

set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\regression"
set "EXE=%ROOT%\build\path_probe.exe"

if not exist "%OUT%" mkdir "%OUT%"

call "%~dp0build_pathprobe.bat" 1869 2881 40 1446 1663 2 -10 > "%OUT%\path_01_down_negz.txt" 2>&1
type "%OUT%\path_01_down_negz.txt"
if errorlevel 1 exit /b 1
findstr /C:"(NO PATH)" "%OUT%\path_01_down_negz.txt" >nul
if not errorlevel 1 (
    echo pathfinding regression failed: 01_down_negz returned NO PATH
    exit /b 1
)

"%EXE%" 1446 1663 -10 1869 2881 2 40 > "%OUT%\path_02_up_z40.txt" 2>&1
type "%OUT%\path_02_up_z40.txt"
if errorlevel 1 exit /b 1
findstr /C:"(NO PATH)" "%OUT%\path_02_up_z40.txt" >nul
if not errorlevel 1 (
    echo pathfinding regression failed: 02_up_z40 returned NO PATH
    exit /b 1
)

echo pathfinding regression routes passed; logs written to %OUT%
exit /b 0
