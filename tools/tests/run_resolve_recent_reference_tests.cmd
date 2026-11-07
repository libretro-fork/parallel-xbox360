@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\resolve-recent-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /MD /O2 /EHsc /std:c++20 /W4 /WX /Ilibretro /Ithird_party/libretro-common/include tools/tests/core_resolve_recent_reference.cc /Fo%OUT%\reference.obj /Fe%OUT%\reference.exe
if errorlevel 1 goto fail
%OUT%\reference.exe %*
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
