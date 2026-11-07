@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SOURCE_VIEW_TEST_OUT set SOURCE_VIEW_TEST_OUT=build\tests\source-views-msvc
set OUT=%SOURCE_VIEW_TEST_OUT%
if not exist %OUT% mkdir %OUT%
cl %SOURCE_VIEW_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX /Fd%OUT%\test.pdb /Ilibretro /Ithird_party/libretro-common/include /Ithird_party/Vulkan-Headers/include tools/tests/core_vk_source_views_test.c /Fo%OUT%\test.obj /Fe%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
