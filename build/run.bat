@REM uo.serpent-isle.com
@REM 

call .\..\scripts\build.bat
:: uo_client.exe uo.serpent-isle.com 2593 xrip xrip
uo_client 172.28.160.1 2593 x1 x 2593
:: uo_client.exe localhost 2593 xrip123 xrip123 2593
:: uo_client.exe localhost 2600 xrip xrip 2600
pause