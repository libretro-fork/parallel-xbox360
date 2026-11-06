@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\vk-sparse-bind-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX /Ilibretro /Ithird_party/libretro-common/include /Ithird_party/Vulkan-Headers/include tools/tests/core_vk_sparse_bind_test.c /Fo%OUT%\test.obj /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
