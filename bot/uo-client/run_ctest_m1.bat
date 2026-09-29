@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Projects\RevolutionOffline\bot\uo-client"
ctest --test-dir build-m1 --output-on-failure
