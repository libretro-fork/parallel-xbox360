@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt; supply built common library.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set OUT=%ROOT%\build\tests\bitmap-msvc
if not exist "%OUT%" mkdir "%OUT%"
if "%~1"=="" (
  echo Usage: %~nx0 path-to-retro-common.lib
  exit /b 2
)
cl /nologo /TC /O2 /MD /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I"%ROOT%" /external:I"%ROOT%\third_party\libretro-common\include" /external:W0 "%ROOT%\tools\tests\core_bitmap_test.c" /Fo"%OUT%\test.obj" /Fe"%OUT%\test.exe" /link "%~1" winmm.lib ws2_32.lib bcrypt.lib
if errorlevel 1 exit /b 1
"%OUT%\test.exe"
exit /b %errorlevel%
