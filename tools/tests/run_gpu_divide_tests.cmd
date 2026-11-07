@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined GPU_DIVIDE_TEST_OUT set GPU_DIVIDE_TEST_OUT=build\tests\gpu-divide-msvc
set OUT=%GPU_DIVIDE_TEST_OUT%
if not exist %OUT% mkdir %OUT%
cl %GPU_DIVIDE_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX /Fd%OUT%\test.pdb /Ilibretro tools/tests/core_gpu_divide_test.c /Fo%OUT%\test.obj /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
