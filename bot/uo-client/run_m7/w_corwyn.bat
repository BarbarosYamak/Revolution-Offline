@echo off
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
"%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
  --user reva01 --pass Fr3sh20BotsRevol --char-name Corwyn ^
  --autonomous --bot-data "%BOT%\bot_data" --life-minutes 10 ^
  --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag v3_Corwyn ^
  --log "%BOT%\run_m7\v3_Corwyn.log" ^
  > "%BOT%\run_m7\v3_Corwyn.console.txt" 2> "%BOT%\run_m7\v3_Corwyn.err.txt"
