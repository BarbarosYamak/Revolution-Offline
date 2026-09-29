@echo off
REM Create the fresh thirty (RevGen3). Each is an ordinary character creation
REM over the real protocol -- no admin, no granted skills, no granted gold.
REM Uses "ping -n 4 127.0.0.1 > nul" to stagger launches instead of `timeout`,
REM which fails when run from a hidden/non-interactive console.
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set UO_BOT_PASS=Gen3Fr3shRevBots
for /f "usebackq tokens=1,2,3 delims=	" %%a in (`findstr /v "^#" "%BOT%\run_gates\roster30.tsv"`) do (
  if not "%%a"=="" (
    echo creating %%a on %%b as %%c
    start "" /b "%BOT%\build-m1\uo_client.exe" --headless ^
      --host 127.0.0.1 --port 2593 ^
      --user %%b --pass %UO_BOT_PASS% ^
      --char-name %%a --create-char ^
      --profession %%c ^
      --autonomous ^
      --bot-data "%BOT%\bot_data" ^
      --life-minutes 3 ^
      --mul-dir "%ROOT%\runtime\mul" ^
      --data-dir "%BOT%\data" ^
      --tag mk3_%%a ^
      --log "%BOT%\run_gates\mk3_%%a.log" ^
      > "%BOT%\run_gates\mk3_%%a.console.txt" 2> "%BOT%\run_gates\mk3_%%a.err.txt"
    ping -n 4 127.0.0.1 > nul
  )
)
echo all launched
