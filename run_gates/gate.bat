@echo off
REM ===========================================================================
REM LIFE-GATE run harness -- ONE family per invocation.
REM Rules, citations and the per-family table: docs/LIFE_GATES.md
REM
REM   gate.bat <family> <char> <account> <minutes> [password]
REM
REM   family    profession id from src/life/Professions.cpp
REM             (miner_smith lumberjack_swordsman mage fencer full_crafter
REM              fisher scribe alchemist)
REM   char      existing character name in bot_data -- NO --create-char here
REM   account   the account that owns it
REM   minutes   --life-minutes; the first wave uses 30 (LIFE_GATES.md section 8)
REM   password  optional; defaults to the shared RevGen3 fleet password.
REM             The three RevArchetype accounts have their own: Archetype31,
REM             Archetype32, Archetype33 (runtime/accounts/sphereaccu.scp).
REM
REM Launcher shape copied from run_r4/smoke.bat: build-m1 exe, shared bot_data,
REM tag g_<char>, console/err redirected into run_gates/.
REM
REM Before launching, state.json is snapshotted to run_gates/g_<char>.state_before.json
REM so the grader has a pre-state to diff the bank against.  The post-state is
REM the live bot_data file, written by the clean-logout checkpoint.
REM
REM Verdicts are read from run_gates\g_<char>.console.txt / .err.txt, never the .log.
REM
REM GRADING (after logout_complete appears in the console):
REM
REM   python ..\tools\grade_life.py g_<char>.console.txt ^
REM       g_<char>.state_before.json ^
REM       ..\bot_data\<account>.<char>\state.json ^
REM       --family <family>
REM
REM   exit 0 = every rule PASSed = the family earns its R5 roster seat.
REM   Anything else prints the failing rule ids (FARM-n / TRAIN-n / STOCK-n / LIVE-n).
REM
REM Worked example:
REM   gate.bat miner_smith Durnholde RevolutionFresh02 30
REM   python ..\tools\grade_life.py g_Durnholde.console.txt g_Durnholde.state_before.json ^
REM       ..\bot_data\RevolutionFresh02.Durnholde\state.json --family miner_smith
REM ===========================================================================
setlocal
set ROOT=C:\Projects\RevolutionOffline
set BOT=%ROOT%\bot\uo-client
set GATES=%BOT%\run_gates
set FAMILY=%1
set CHAR=%2
set ACCOUNT=%3
set MINUTES=%4
set UO_BOT_PASS=%5
if "%UO_BOT_PASS%"=="" set UO_BOT_PASS=Gen3Fr3shRevBots

if "%FAMILY%"==""  goto :usage
if "%CHAR%"==""    goto :usage
if "%ACCOUNT%"=="" goto :usage
if "%MINUTES%"=="" set MINUTES=30

set STATE=%BOT%\bot_data\%ACCOUNT%.%CHAR%\state.json
set BEFORE=%GATES%\g_%CHAR%.state_before.json

REM --- pre-state snapshot (NTFS is case-insensitive, so the account.char
REM     folder resolves whatever the caller's capitalisation) -----------------
if exist "%STATE%" (
  copy /y "%STATE%" "%BEFORE%" > nul
  echo pre-state snapshot -^> %BEFORE%
) else (
  echo WARNING: no state.json at %STATE%
  echo   -- writing an empty pre-state so the grader still runs.
  echo   -- STOCK-1 will read the bank as starting from zero.
  echo {"bank":[]}> "%BEFORE%"
)

echo gate: %CHAR% on %ACCOUNT% as %FAMILY% for %MINUTES% min
start "" /b "%BOT%\build-m1\uo_client.exe" --headless ^
  --host 127.0.0.1 --port 2593 ^
  --user %ACCOUNT% --pass %UO_BOT_PASS% ^
  --char-name %CHAR% ^
  --profession %FAMILY% ^
  --autonomous ^
  --bot-data "%BOT%\bot_data" ^
  --life-minutes %MINUTES% ^
  --mul-dir "%ROOT%\runtime\mul" ^
  --data-dir "%BOT%\data" ^
  --tag g_%CHAR% ^
  --log "%GATES%\g_%CHAR%.log" ^
  > "%GATES%\g_%CHAR%.console.txt" 2> "%GATES%\g_%CHAR%.err.txt"

echo launched. when the console ends in logout_complete, grade with:
echo   python "%BOT%\tools\grade_life.py" "%GATES%\g_%CHAR%.console.txt" "%BEFORE%" "%STATE%" --family %FAMILY%
goto :eof

:usage
echo usage: gate.bat ^<family^> ^<char^> ^<account^> ^<minutes^>
echo   e.g. gate.bat miner_smith Durnholde RevolutionFresh02 30
echo   families: miner_smith lumberjack_swordsman mage fencer full_crafter fisher scribe alchemist
exit /b 2
