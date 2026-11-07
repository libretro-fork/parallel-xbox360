@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined GPU_TASK_TEST_OUT set GPU_TASK_TEST_OUT=build\tests\gpu-tasks-msvc
set OUT=%GPU_TASK_TEST_OUT%
if not exist %OUT% mkdir %OUT%
set FLAGS=%GPU_TASK_TEST_FLAGS% /nologo /TC /MD /O2 /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00
set INC=/I. /Ilibretro /Ithird_party/libretro-common/include
cl %FLAGS% %INC% /c third_party/libretro-common/rthreads/rthreads.c /Fd%OUT%\rthreads.pdb /Fo%OUT%\rthreads.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /W4 /WX %INC% /FItools/tests/core_gpu_tasks_hook.h /c libretro/core_gpu_tasks.c /Fd%OUT%\tasks.pdb /Fo%OUT%\tasks.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /W4 /WX %INC% /c tools/tests/core_gpu_tasks_test.c /Fd%OUT%\test.pdb /Fo%OUT%\test.obj
if not "%errorlevel%"=="0" goto fail
cl %GPU_TASK_TEST_FLAGS% /nologo /MD /O2 /EHsc /std:c++20 /W4 /WX %INC% /c tools/tests/core_gpu_tasks_bridge.cc /Fd%OUT%\bridge.pdb /Fo%OUT%\bridge.obj
if not "%errorlevel%"=="0" goto fail
cl %GPU_TASK_TEST_FLAGS% /nologo /MD %OUT%\bridge.obj %OUT%\test.obj %OUT%\tasks.obj %OUT%\rthreads.obj winmm.lib /Fe%OUT%\gpu-control-check.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\gpu-control-check.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
