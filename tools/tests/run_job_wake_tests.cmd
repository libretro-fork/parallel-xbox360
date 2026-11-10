@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\job-wake-msvc
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00 /I. /Ithird_party/libretro-common/include tools/tests/core_job_wake_test.c third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c /Fo%OUT%\ /Fe"%OUT%\test.exe" /link winmm.lib
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00 /I. /Ithird_party/libretro-common/include tools/tests/core_job_wake_handles_test.c third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c /Fo%OUT%\ /Fe"%OUT%\handles.exe" /link winmm.lib
if errorlevel 1 goto fail
"%OUT%\handles.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
