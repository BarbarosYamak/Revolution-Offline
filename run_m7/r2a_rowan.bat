@echo off
REM R2 -- the first lawful kill, through Classify()/ChooseTarget().
REM Composed to fire ONE unfired capability: a fighter that hunts.
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
"%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
  --user RevolutionNew10 --pass Fr3sh20BotsRevol --char-name Rowan ^
  --autonomous --bot-data "%BOT%\bot_data" --life-minutes 30 ^
  --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag r2a_Rowan ^
  --log "%BOT%\run_m7\r2a_Rowan.log" ^
  > "%BOT%\run_m7\r2a_Rowan.console.txt" 2> "%BOT%\run_m7\r2a_Rowan.err.txt"
