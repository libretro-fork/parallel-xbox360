@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\snapshot-pair-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX /Ilibretro /Ithird_party/libretro-common/include tools/tests/core_snapshot_pair_test.c /Fo%OUT%\leaf.obj /Fe%OUT%\leaf.exe
if errorlevel 1 goto fail
%OUT%\leaf.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
