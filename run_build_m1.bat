@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Projects\RevolutionOffline\bot\uo-client"
cmake --build build-m1
