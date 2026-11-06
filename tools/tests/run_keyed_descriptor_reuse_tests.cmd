@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\keyed-descriptor-msvc
if not exist %OUT% mkdir %OUT%
set INC=/Ilibretro /Ithird_party/libretro-common/include /Ithird_party/Vulkan-Headers/include
cl /nologo /TC /MD /O2 /W4 /WX %INC% tools/tests/core_keyed_descriptor_reuse_test.c /Fo%OUT%\leaf.obj /Fe%OUT%\leaf.exe
if errorlevel 1 goto fail
%OUT%\leaf.exe
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 %INC% tools/tests/keyed_descriptor_reuse_reference.cc /Fo%OUT%\reference.obj /Fe%OUT%\reference.exe
if errorlevel 1 goto fail
%OUT%\reference.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
