@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\resolve-readback-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX /Ilibretro /Ithird_party/libretro-common/include /Ithird_party/Vulkan-Headers/include tools/tests/core_resolve_readback_test.c /Fo%OUT%\leaf.obj /Fe%OUT%\leaf.exe
if errorlevel 1 goto fail
%OUT%\leaf.exe
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /Ilibretro /Ithird_party/libretro-common/include tools/tests/core_resolve_readback_owner_test.cc /Fo%OUT%\owner.obj /Fe%OUT%\owner.exe
if errorlevel 1 goto fail
%OUT%\owner.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
