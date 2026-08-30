@echo off
REM Archetype smoke runs after the 2026-08-30 audit fixes: mage heal loop,
REM fencer market spin, full_crafter tools/bandages. 12 minutes each.
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set UO_BOT_PASS=Fr3sh20BotsRevol
for %%x in ("Ilyandra:RevolutionFresh13:mage" "Kaelen:RevolutionFresh07:fencer" "Bruin:RevolutionFresh16:full_crafter") do (
  for /f "tokens=1,2,3 delims=:" %%a in (%%x) do (
    echo launching %%a on %%b as %%c
    start "" /b "%BOT%\build-m1\uo_client.exe" --headless ^
      --host 127.0.0.1 --port 2593 ^
      --user %%b --pass %UO_BOT_PASS% ^
      --char-name %%a --profession %%c ^
      --autonomous --bot-data "%BOT%\bot_data" ^
      --life-minutes 12 ^
      --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" ^
      --tag w_%%a --log "%BOT%\run_r4\w_%%a.log" ^
      > "%BOT%\run_r4\w_%%a.console.txt" 2> "%BOT%\run_r4\w_%%a.err.txt"
    ping -n 4 127.0.0.1 > nul
  )
)
echo all launched
