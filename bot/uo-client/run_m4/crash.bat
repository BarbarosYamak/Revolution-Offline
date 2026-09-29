@echo off
REM M4 Slice 1 Phase 21 -- UNCLEAN termination.
REM
REM Starts a session, lets it run long enough to learn something and take a
REM periodic checkpoint, then kills the process outright. No logout, no
REM session summary, no final save. What survives is whatever the last bounded
REM checkpoint wrote -- which is exactly the thing the crash-safety proof is
REM about.
setlocal
set BOT=C:\Projects\RevolutionOffline\bot\uo-client
start "" /b cmd /c "%BOT%\run_m4\session.bat crash 30"
echo started; waiting %1 seconds before the kill
timeout /t %1 /nobreak > nul
taskkill /F /IM uo_client.exe > nul 2>&1
echo KILLED uo_client.exe with no clean logout
