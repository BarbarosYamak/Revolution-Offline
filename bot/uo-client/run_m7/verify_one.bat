@echo off
REM ACCOUNT-BY-ACCOUNT VERIFICATION. One character, one profession, one flow,
REM watched end to end before the next is created. Args: account char profession
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
start "" /b "%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
  --user %1 --pass Fr3sh20BotsRevol --char-name %2 --create-char --profession %3 ^
  --autonomous --bot-data "%BOT%\bot_data" --life-minutes %4 ^
  --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag v_%2 ^
  --log "%BOT%\run_m7\v_%2.log" ^
  > "%BOT%\run_m7\v_%2.console.txt" 2> "%BOT%\run_m7\v_%2.err.txt"
