@echo off
REM Six of the fresh twenty, chosen so both ends of every chain are present:
REM   Brannoc  miner_smith  -> ingots     -> Bruin
REM   Tarath   lumberjack   -> logs       -> Bruin (spear = 6 ingots + 1 log)
REM   Voris    alchemist    -> heal potions -> Kaelen (no NPC sells them)
REM   Kaelen   fencer       -> loot, and the alchemist's customer
REM   Ysolde   scribe       -> scrolls, the one proven NPC income
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set PW=Fr3sh20BotsRevol
set MIN=%1
if "%MIN%"=="" set MIN=25
for %%p in ("RevolutionFresh01 miner_smith Brannoc" "RevolutionFresh03 lumberjack_swordsman Tarath" "RevolutionFresh05 alchemist Voris" "RevolutionFresh07 fencer Kaelen" "RevolutionFresh11 scribe Ysolde" "RevolutionFresh16 full_crafter Bruin") do (
  for /f "tokens=1,2,3" %%a in (%%p) do (
    echo launching %%c
    start "" /b "%BOT%\build-m1\uo_client.exe" --headless --host 127.0.0.1 --port 2593 ^
      --user %%a --pass %PW% --char-name %%c --create-char --profession %%b ^
      --autonomous --bot-data "%BOT%\bot_data" --life-minutes %MIN% ^
      --mul-dir "%ROOT%\runtime\mul" --data-dir "%BOT%\data" --tag f6_%%c ^
      --log "%BOT%\run_m7\f6_%%c.log" ^
      > "%BOT%\run_m7\f6_%%c.console.txt" 2> "%BOT%\run_m7\f6_%%c.err.txt"
    timeout /t 4 /nobreak > nul
  )
)
echo six launched
