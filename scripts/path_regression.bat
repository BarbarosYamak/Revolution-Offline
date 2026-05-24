@echo off
setlocal
rem Pathfinding regression harness.
rem
rem Runs two long cross-continent A* routes (Trinsic bridge <-> Britain
rem basement, there and back) and writes the metrics to a committed golden
rem file (tests\path_regression.txt). After ANY change under src\bot\ or to
rem World::QueryCell, re-run this and `git diff tests\path_regression.txt`:
rem   - result / steps / expanded / pathCost are deterministic -> a diff there
rem     is a real behavior change, investigate it.
rem   - searchUs is wall-clock timing (machine/run dependent) -> a trend, not
rem     a hard check; large regressions still matter.
rem
rem   scripts\path_regression.bat

set "ROOT=%~dp0.."
set "OUT=%ROOT%\tests\path_regression.txt"
set "EXE=%ROOT%\build\path_probe.exe"
rem Do NOT name this TMP/TEMP: MSVC (cl.exe) uses those for its own scratch.
set "RAW=%ROOT%\build\path_regress_raw.txt"

rem Build + run route A (the build script compiles then runs with these args).
call "%~dp0build_pathprobe.bat" 1869 2881 40 1446 1663 8 -10 > "%RAW%" 2>&1
if errorlevel 1 (
    echo build/run failed:
    type "%RAW%"
    del "%RAW%" 2>nul
    exit /b 1
)

> "%OUT%" echo # Pathfinding regression metrics -- golden baseline.
>> "%OUT%" echo # Re-run scripts\path_regression.bat after ANY pathfinding change and diff this file.
>> "%OUT%" echo # Deterministic regression signal: result / steps / expanded / pathCost.
>> "%OUT%" echo # searchUs is wall-clock timing: machine/run dependent, treat as a trend.
>> "%OUT%" echo # Waypoints: trinsic_bridge=1869,2881,40   brit_down=1446,1663,-10
>> "%OUT%" echo.
>> "%OUT%" echo == trinsic_bridge -^> brit_down  (1869,2881,40 -^> 1446,1663,-10) ==
findstr /b /c:"start " /c:"penalty=" "%RAW%" >> "%OUT%"

rem Route B (reuse the exe just built).
"%EXE%" 1446 1663 -10 1869 2881 8 40 > "%RAW%" 2>&1
>> "%OUT%" echo.
>> "%OUT%" echo == brit_down -^> trinsic_bridge  (1446,1663,-10 -^> 1869,2881,40) ==
findstr /b /c:"start " /c:"penalty=" "%RAW%" >> "%OUT%"

del "%RAW%" 2>nul
echo ---- wrote %OUT% ----
type "%OUT%"
exit /b 0
