@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined VBLANK_TEST_OUT set VBLANK_TEST_OUT=build\tests\vblank-state-msvc
set OUT=%VBLANK_TEST_OUT%
if not exist %OUT% mkdir %OUT%
set FLAGS=%VBLANK_TEST_FLAGS% /nologo /TC /MD /O2 /Gy /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00
set INC=/I. /Ithird_party/libretro-common/include
cl %FLAGS% /W4 /WX %INC% /c libretro/core_vblank.c libretro/core_pause_gate.c tools/tests/core_vblank_test.c tools/tests/core_state_budget_test.c /Fo%OUT%\ /Fd%OUT%\tests.pdb
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% %INC% /c third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c third_party/libretro-common/features/features_cpu.c third_party/libretro-common/compat/compat_strl.c third_party/libretro-common/time/rtime.c /Fo%OUT%\ /Fd%OUT%\common.pdb
if not "%errorlevel%"=="0" goto fail
cl %VBLANK_TEST_FLAGS% /nologo /MD %OUT%\core_state_budget_test.obj /Fe%OUT%\budget-check.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\budget-check.exe
if not "%errorlevel%"=="0" goto fail
cl %VBLANK_TEST_FLAGS% /nologo /MD %OUT%\core_vblank_test.obj %OUT%\core_vblank.obj %OUT%\core_pause_gate.obj %OUT%\rthreads.obj %OUT%\retro_eventcount.obj %OUT%\features_cpu.obj %OUT%\compat_strl.obj %OUT%\rtime.obj winmm.lib /Fe%OUT%\vblank-check.exe /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
%OUT%\vblank-check.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
