@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined STATE_CONTROL_TEST_OUT set STATE_CONTROL_TEST_OUT=build\tests\state-control-msvc
set OUT=%STATE_CONTROL_TEST_OUT%
if not exist %OUT% mkdir %OUT%
set FLAGS=%STATE_CONTROL_TEST_FLAGS% /nologo /TC /MD /O2 /Gy /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00
set INC=/I. /Ithird_party/libretro-common/include
cl %FLAGS% /W4 /WX %INC% /c libretro/core_state_control.c tools/tests/core_state_control_test.c /Fo%OUT%\ /Fd%OUT%\tests.pdb
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% %INC% /c third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c third_party/libretro-common/features/features_cpu.c third_party/libretro-common/compat/compat_strl.c third_party/libretro-common/time/rtime.c /Fo%OUT%\ /Fd%OUT%\common.pdb
if not "%errorlevel%"=="0" goto fail
cl %STATE_CONTROL_TEST_FLAGS% /nologo /MD %OUT%\core_state_control_test.obj %OUT%\core_state_control.obj %OUT%\rthreads.obj %OUT%\retro_eventcount.obj %OUT%\features_cpu.obj %OUT%\compat_strl.obj %OUT%\rtime.obj winmm.lib /Fe%OUT%\control-check.exe /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
%OUT%\control-check.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
