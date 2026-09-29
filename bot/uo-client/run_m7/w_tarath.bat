@echo off
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
"%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
  --user RevolutionFresh03 --pass Fr3sh20BotsRevol --char-name Tarath ^
  --autonomous --bot-data "%BOT%\bot_data" --life-minutes 12 ^
  --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag w_Tarath ^
  --log "%BOT%\run_m7\w_Tarath.log" ^
  > "%BOT%\run_m7\w_Tarath.console.txt" 2> "%BOT%\run_m7\w_Tarath.err.txt"
