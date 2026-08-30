@echo off
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
start "" /b "%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
  --user RevolutionFresh06 --pass Fr3sh20BotsRevol --char-name Ilyana ^
  --create-char --profession alchemist --autonomous ^
  --bot-data "%BOT%\bot_data" --life-minutes 25 ^
  --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag ilyana ^
  --log "%BOT%\run_m7\ilyana.log" ^
  > "%BOT%\run_m7\ilyana.console.txt" 2> "%BOT%\run_m7\ilyana.err.txt"
