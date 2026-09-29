@echo off
REM M4 Slice 1 -- one autonomous session for the frontier Lumberjack/Swordsman.
REM
REM There is NO --scenario here, and that is the point: nothing on this command
REM line says travel, chop, bank or log out. The character decides all of that
REM from what it can observe, and its identity and learned knowledge live under
REM --bot-data across logouts and host restarts.
REM
REM   %1  session tag (e.g. sessionA)
REM   %2  minutes before a clean logout (default 30)
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set TAG=%1
if "%TAG%"=="" set TAG=life
set MINUTES=%2
if "%MINUTES%"=="" set MINUTES=30

"%BOT%\build-m1\uo_client.exe" ^
  --headless ^
  --host 127.0.0.1 --port 2593 ^
  --user %UO_BOT_USER% --pass %UO_BOT_PASS% ^
  --char-name Tarath --create-char ^
  --create-skills "44:40,40:40,17:20" ^
  --create-stats "40:35:5" ^
  --autonomous ^
  --bot-data "%BOT%\bot_data" ^
  --life-minutes %MINUTES% ^
  --mul-dir "%ROOT%\runtime\mul" ^
  --data-dir "%BOT%\data" ^
  --tag %TAG% ^
  --log "%BOT%\run_m4\%TAG%.log" ^
  > "%BOT%\run_m4\%TAG%.console.txt" 2> "%BOT%\run_m4\%TAG%.err.txt"
echo exit=%errorlevel%
