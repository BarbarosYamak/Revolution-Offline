@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /I "%~dp0..\include" /I "%~dp0..\src" "%~dp0..\tests\huffman_roundtrip.cpp" "%~dp0..\src\net\Huffman.cpp" /Fe:"%~dp0..\build\huff_test.exe" /Fo:"%~dp0..\build\\"
if errorlevel 1 exit /b 1
"%~dp0..\build\huff_test.exe"
exit /b %errorlevel%
