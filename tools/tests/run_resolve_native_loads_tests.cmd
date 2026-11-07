@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined RESOLVE_LOAD_TEST_OUT set RESOLVE_LOAD_TEST_OUT=build\tests\resolve-native-loads-msvc
set OUT=%RESOLVE_LOAD_TEST_OUT%
if not exist %OUT% mkdir %OUT%
cl %RESOLVE_LOAD_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX /Fd%OUT%\test.pdb /Ilibretro tools/tests/core_resolve_native_loads_test.c /Fo%OUT%\test.obj /Fe%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
