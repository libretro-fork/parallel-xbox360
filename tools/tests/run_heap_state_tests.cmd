@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined HEAP_STATE_TEST_OUT set HEAP_STATE_TEST_OUT=build\tests\heap-state-msvc
set OUT=%HEAP_STATE_TEST_OUT%
if not exist %OUT% mkdir %OUT%
cl %HEAP_STATE_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party/libretro-common/include tools/tests/core_heap_state_test.c /Fo%OUT%\heap.obj /Fd%OUT%\heap.pdb /Fe%OUT%\heap-plan.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\heap-plan.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
