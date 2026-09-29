@echo off
REM M5 -- one autonomous session for a NEW life named by --profession.
REM
REM There is no --create-skills and no --create-stats here on purpose: the
REM profession catalogue IS the creation request (Revolution's rule -- exactly
REM two skills at 50.0 and about 50 stat points), so a character's opening hand
REM is decided in one place and can be tested without a server.
REM
REM   %1  profession id   (lumberjack_swordsman | miner_smith | mage | ...)
REM   %2  character name
REM   %3  session tag
REM   %4  minutes before a clean logout (default 20)
REM
REM Credentials come from UO_BOT_USER / UO_BOT_PASS, never from this file.
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set PROF=%1
set CHAR=%2
set TAG=%3
if "%TAG%"=="" set TAG=m5
set MINUTES=%4
if "%MINUTES%"=="" set MINUTES=20

"%BOT%\build-m1\uo_client.exe" ^
  --headless ^
  --host 127.0.0.1 --port 2593 ^
  --user %UO_BOT_USER% --pass %UO_BOT_PASS% ^
  --char-name %CHAR% --create-char ^
  --profession %PROF% ^
  --autonomous ^
  --bot-data "%BOT%\bot_data" ^
  --life-minutes %MINUTES% ^
  --mul-dir "%ROOT%\runtime\mul" ^
  --data-dir "%BOT%\data" ^
  --tag %TAG% ^
  --log "%BOT%\run_m5\%TAG%.log" ^
  > "%BOT%\run_m5\%TAG%.console.txt" 2> "%BOT%\run_m5\%TAG%.err.txt"
echo exit=%errorlevel%
