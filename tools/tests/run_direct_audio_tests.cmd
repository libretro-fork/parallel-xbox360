@echo off
setlocal
rem Run from an x64 MSVC developer prompt after building retro-common.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set BUILD=%~1
if not defined BUILD set BUILD=%ROOT%\build\cmake-msvc-native
set OUT=%ROOT%\build\tests\audio-direct-msvc
if defined TEST_BUILD_DIR set OUT=%TEST_BUILD_DIR%
if not exist "%OUT%" mkdir "%OUT%"
set COMMON=%ROOT%\third_party\libretro-common
set COMMONLIB=%BUILD%\obj\Windows\retro-common.lib
if not exist "%COMMONLIB%" exit /b 1
pushd "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /I"%ROOT%\libretro" "%ROOT%\libretro\core_audio_convert.c" "%ROOT%\tools\tests\core_audio_convert_test.c" /Feconvert.exe
if errorlevel 1 goto fail
convert.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I"%COMMON%\include" /I"%ROOT%\libretro" "%ROOT%\tools\tests\core_audio_borrow_test.c" /Feborrow.exe /link "%COMMONLIB%" winmm.lib ws2_32.lib bcrypt.lib
if errorlevel 1 goto fail
borrow.exe
if errorlevel 1 goto fail
for %%T in (core_audio_queue_test core_audio_demand_test) do (
  for %%P in (0 1) do (
    call :queue %%T %%P
    if errorlevel 1 goto fail
  )
)
popd
exit /b 0
:queue
set PLAIN=
if "%2"=="1" set PLAIN=/DXE_AUDIO_QUEUE_PLAIN
cl /nologo /TC /MD /O2 /W4 /WX %PLAIN% /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I"%COMMON%\include" /I"%ROOT%\libretro" "%ROOT%\libretro\core_audio_convert.c" "%ROOT%\libretro\core_audio_queue.c" "%ROOT%\tools\tests\%1.c" /Fequeue-%1-%2.exe /link "%COMMONLIB%" winmm.lib ws2_32.lib bcrypt.lib
if errorlevel 1 exit /b 1
queue-%1-%2.exe
exit /b %errorlevel%
:fail
popd
exit /b 1
