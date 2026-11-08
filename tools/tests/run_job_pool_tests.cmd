@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined JOB_POOL_TEST_OUT set JOB_POOL_TEST_OUT=build\tests\job-pool-msvc
set OUT=%JOB_POOL_TEST_OUT%
if not exist %OUT% mkdir %OUT%
set FLAGS=%JOB_POOL_TEST_FLAGS% /nologo /TC /MD /O2 /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00
set INC=/I. /Isrc /Ilibretro /Ithird_party/libretro-common/include
cl %FLAGS% %INC% /c libretro/core_job_pool.c third_party/libretro-common/rthreads/rthreads.c third_party/libretro-common/rthreads/retro_eventcount.c /Fo%OUT%\ /Fd%OUT%\pool.pdb
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /W4 /WX %INC% /c tools/tests/core_completion_test.c /Fo%OUT%\completion.obj /Fd%OUT%\completion.pdb
if not "%errorlevel%"=="0" goto fail
cl %JOB_POOL_TEST_FLAGS% /nologo /MD %OUT%\completion.obj %OUT%\rthreads.obj winmm.lib /Fe%OUT%\completion-check.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\completion-check.exe
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% %INC% /c tools/tests/core_job_pool_test.c /Fo%OUT%\pool-test.obj /Fd%OUT%\pool-test.pdb
if not "%errorlevel%"=="0" goto fail
cl %JOB_POOL_TEST_FLAGS% /nologo /MD %OUT%\pool-test.obj %OUT%\core_job_pool.obj %OUT%\rthreads.obj %OUT%\retro_eventcount.obj winmm.lib /Fe%OUT%\pool-check.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\pool-check.exe
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /W4 /WX %INC% /c tools/tests/core_pipeline_ready_test.c /Fo%OUT%\ready.obj /Fd%OUT%\ready.pdb
if not "%errorlevel%"=="0" goto fail
cl %JOB_POOL_TEST_FLAGS% /nologo /MD /O2 /EHsc /std:c++20 /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC %INC% tools/tests/core_job_pool_bridge.cc tools/tests/core_pipeline_ready_bridge.cc %OUT%\ready.obj %OUT%\core_job_pool.obj %OUT%\rthreads.obj %OUT%\retro_eventcount.obj winmm.lib /Fe%OUT%\adapter-check.exe /Fo%OUT%\ /Fd%OUT%\adapter.pdb
if not "%errorlevel%"=="0" goto fail
%OUT%\adapter-check.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
