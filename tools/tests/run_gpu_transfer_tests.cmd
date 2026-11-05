@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set OUT=%ROOT%\build\tests\gpu-transfer-msvc
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /O2 /MD /W4 /WX /I"%ROOT%" /c "%ROOT%\libretro\core_gpu_worklist.c" /Fo"%OUT%\worklist.obj"
if errorlevel 1 exit /b 1
cl /nologo /TC /O2 /MD /W4 /WX /I"%ROOT%" "%ROOT%\tools\tests\core_gpu_transfer_test.c" /Fo"%OUT%\test.obj" /Fe"%OUT%\test.exe" /link "%OUT%\worklist.obj"
if errorlevel 1 exit /b 1
"%OUT%\test.exe"
exit /b %errorlevel%
