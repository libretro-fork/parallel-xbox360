@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\vk-pool-owner-msvc
if not exist %OUT% mkdir %OUT%
set INC=/Itools/tests/vk_pool_stubs /I. /Isrc /Ilibretro /Ithird_party/libretro-common/include /Ithird_party/Vulkan-Headers/include
cl /nologo /TC /MD /O2 /W4 /WX %INC% tools/tests/core_vk_pool_owner_test.c /Fo%OUT%\leaf.obj /Fe%OUT%\leaf.exe
if errorlevel 1 goto fail
%OUT%\leaf.exe
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG %INC% /c src/xenia/ui/vulkan/linked_type_descriptor_set_allocator.cc /Fo%OUT%\allocator.obj
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG %INC% tools/tests/vk_pool_owner_bridge.cc %OUT%\allocator.obj /Fo%OUT%\bridge.obj /Fe%OUT%\bridge.exe
if errorlevel 1 goto fail
%OUT%\bridge.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
