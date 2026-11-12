@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined VK_UPLOAD_FLUSH_OUT set VK_UPLOAD_FLUSH_OUT=build\tests\vk-upload-flush-msvc
if not exist "%VK_UPLOAD_FLUSH_OUT%" mkdir "%VK_UPLOAD_FLUSH_OUT%"
cl /nologo /TC /O2 /W4 /WX /I. /Ithird_party\libretro-common\include /Ithird_party\Vulkan-Headers\include tools\tests\core_vk_upload_flush_test.c /Fo"%VK_UPLOAD_FLUSH_OUT%\test.obj" /Fe"%VK_UPLOAD_FLUSH_OUT%\test.exe"
if errorlevel 1 goto fail
"%VK_UPLOAD_FLUSH_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
