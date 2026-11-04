@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set OUT=%ROOT%\build\tests\log-line-msvc
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /I"%ROOT%" "%ROOT%\tools\tests\core_log_line_test.c" /Fo"%OUT%\test.obj" /Fe"%OUT%\test.exe"
if errorlevel 1 exit /b 1
"%OUT%\test.exe"
exit /b %errorlevel%
