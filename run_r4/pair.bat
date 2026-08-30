@echo off
REM R4 -- the first player-to-player trade, deliberately composed.
REM docs/CRAFTER_RUN_2026_08_30.md "The run -- run_r4/pair" and docs/S5_MARKET_TRIP_PLAN.md.
REM
REM Two EXISTING characters, no --create-char:
REM   Tarath    RevolutionFresh03  lumberjack_swordsman  home Britain, 113 logs banked (seller)
REM   Durnholde RevolutionFresh02  miner_smith           home Minoc, wants i_log below 20 (buyer)
REM They meet at the shard's market bank (britain_bank_2) via TRADE_WITH_PLAYER (S5).
REM
REM   %1  minutes (default 60 -- S5 plan section 3: one Minoc->Britain leg is 250 s,
REM       a market attempt 560 s, cooldown 10 min; hard floor 25)
REM   %2  tag prefix (default pair)
REM
REM Verdicts are read from run_r4\<tag>.console.txt / .err.txt, never the .log:
REM   grep "trade: gave" / "trade: got" / GOLD_TRANSFER_PLAYERTRADE / session_goals / market:
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set UO_BOT_PASS=Fr3sh20BotsRevol
set MINUTES=%1
if "%MINUTES%"=="" set MINUTES=60
set TAG=%2
if "%TAG%"=="" set TAG=pair

REM Durnholde FIRST: his Minoc->Britain leg is ~250 s; Tarath's walk is ~60 s.
REM Launching the buyer 4 min ahead puts both at britain_bank_2 inside the same
REM listen window (review finding: unsynchronised 10-min stand-downs).
call :launch Durnholde RevolutionFresh02 miner_smith
echo waiting 240 s before the seller...
ping -n 241 127.0.0.1 > nul
call :launch Tarath    RevolutionFresh03 lumberjack_swordsman
echo both launched, tag %TAG%, %MINUTES% minutes
goto :eof

:launch
echo launching %1 on %2 as %3
start "" /b "%BOT%\build-m1\uo_client.exe" --headless ^
  --host 127.0.0.1 --port 2593 ^
  --user %2 --pass %UO_BOT_PASS% ^
  --char-name %1 ^
  --profession %3 ^
  --autonomous ^
  --bot-data "%BOT%\bot_data" ^
  --life-minutes %MINUTES% ^
  --mul-dir "%ROOT%\runtime\mul" ^
  --data-dir "%BOT%\data" ^
  --tag %TAG%_%1 ^
  --log "%BOT%\run_r4\%TAG%_%1.log" ^
  > "%BOT%\run_r4\%TAG%_%1.console.txt" 2> "%BOT%\run_r4\%TAG%_%1.err.txt"
goto :eof
