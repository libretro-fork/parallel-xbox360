@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\resolve-dispatch-msvc
if not exist %OUT% mkdir %OUT%
cl %RESOLVE_DISPATCH_FLAGS% /nologo /TC /MD /O2 /W4 /WX /Fd%OUT%\test.pdb /Ilibretro /Ithird_party/libretro-common/include tools/tests/core_resolve_dispatch_test.c /Fo%OUT%\leaf.obj /Fe%OUT%\leaf.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\leaf.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
