@echo off
setlocal
rem Renderer visual-regression harness.
rem
rem Every scene below is a world coordinate that once exposed a draw-order /
rem layering bug in src/render/Renderer.cpp. After ANY renderer change, run this
rem and eyeball each PNG in build\regression\ against the official 2.0.7 client.
rem
rem   scripts\render_regression.bat
rem
rem Args to world_viewer.exe: camX camY width height scale camZ outPng
rem (camZ matters for the roof/floor cutoff; pick the player's z in that scene.)

set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\regression"
set "EXE=%ROOT%\build\world_viewer.exe"

if not exist "%OUT%" mkdir "%OUT%"

rem Compile the viewer once (build_viewer.bat also dumps the first scene).
call "%~dp0build_viewer.bat" 2000 2803 800 600 1 0 "%OUT%\01_coast.png"
if errorlevel 1 exit /b 1

rem 02 beige building: upper floor must not bleed over the wall below it.
"%EXE%" 1510 1619 420 340 1 30 "%OUT%\02_beige_wall.png"
if errorlevel 1 exit /b 1

rem 03 stone keep: interior pavers stay bounded by the exterior walls.
"%EXE%" 1515 1616 380 300 1 10 "%OUT%\03_stone_keep.png"
if errorlevel 1 exit /b 1

rem 04 castle battlement: smooth floor, wall tops stay below the planks.
"%EXE%" 1805 2818 800 600 1 40 "%OUT%\04_castle_battlement.png"
if errorlevel 1 exit /b 1

rem 05 three-storey house: z20 floor should not bleed over table/stairs.
"%EXE%" 1810 2806 800 600 1 20 "%OUT%\05_floor2_table_stairs.png"
if errorlevel 1 exit /b 1

rem 06 forest: foliage canopies draw over trunks.
"%EXE%" 1706 2651 800 600 1 0 "%OUT%\06_forest_foliage.png"
if errorlevel 1 exit /b 1

rem 07 negative-Z interior: terrain above the room is culled and floors stay
rem behind walls whose volume crosses the floor z.
"%EXE%" 1446 1663 800 600 1 -10 "%OUT%\07_negz_interior.png"
if errorlevel 1 exit /b 1

echo regression scenes dumped to %OUT%
exit /b 0
