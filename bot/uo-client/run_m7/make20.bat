@echo off
REM Create the fresh twenty. Each is an ordinary character creation over the
REM real protocol -- no admin, no granted skills, no granted gold.
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set UO_BOT_PASS=Fr3sh20BotsRevol
for /f "usebackq tokens=1,2,3 delims=	" %%a in (`findstr /v "^#" "%BOT%\run_m7\roster20.tsv"`) do (
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
      --tag mk_%%a ^
      --log "%BOT%\run_m7\mk_%%a.log" ^
      > "%BOT%\run_m7\mk_%%a.console.txt" 2> "%BOT%\run_m7\mk_%%a.err.txt"
    timeout /t 3 /nobreak > nul
  )
)
echo all launched
