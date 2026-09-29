@echo off
REM The ten NEW characters. Created after the 2026-08-29 kit and build fixes,
REM so unlike the fresh twenty they actually receive scissors, tinker tools and
REM (for the full crafter) the blacksmith kit its own tool list asks for.
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set PW=Fr3sh20BotsRevol
set MIN=%1
if "%MIN%"=="" set MIN=25
for %%p in ("RevolutionNew01 miner_smith Corran" "RevolutionNew02 full_crafter Edrik" "RevolutionNew03 fencer Maribel" "RevolutionNew04 macer Halric" "RevolutionNew05 archer Sianna" "RevolutionNew06 alchemist Ovid" "RevolutionNew07 scribe Thessaly" "RevolutionNew08 mage Wystan" "RevolutionNew09 tailor Delwyn" "RevolutionNew10 lumberjack_swordsman Rowan") do (
  for /f "tokens=1,2,3" %%a in (%%p) do (
    echo launching %%c
    start "" /b "%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
      --user %%a --pass %PW% --char-name %%c --create-char --profession %%b ^
      --autonomous --bot-data "%BOT%\bot_data" --life-minutes %MIN% ^
      --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag n10_%%c ^
      --log "%BOT%\run_m7\n10_%%c.log" ^
      > "%BOT%\run_m7\n10_%%c.console.txt" 2> "%BOT%\run_m7\n10_%%c.err.txt"
    timeout /t 4 /nobreak > nul
  )
)
echo ten launched
