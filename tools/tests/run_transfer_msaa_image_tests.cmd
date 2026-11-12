@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined MSAA_IMAGE_OUT set MSAA_IMAGE_OUT=build\tests\transfer-msaa-image-msvc
set OUT=%MSAA_IMAGE_OUT%
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
set UINT_IMAGE_OUT=%OUT%
call tools\tests\run_transfer_uint_image_tests.cmd
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/SPIRV-Headers/include tools/tests/transfer_msaa_spv_fixture.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/msaa-spv.exe"
if errorlevel 1 exit /b 1
"%OUT%/msaa-spv.exe" "%OUT%/transfer"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. /Isrc/xenia/gpu/dxil/bytecode tools/tests/transfer_msaa_dxil_fixture.c /Fo"%OUT%/" /Fe"%OUT%/msaa-dxil.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib"
if errorlevel 1 exit /b 1
"%OUT%/msaa-dxil.exe" "%OUT%/transfer"
if errorlevel 1 exit /b 1
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/transfer-msaa.list"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party/Vulkan-Headers/include /Ithird_party/SPIRV-Headers/include tools/tests/transfer_msaa_image_vulkan_test.c libretro/core_transfer_uint_vk.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/msaa-vulkan.exe"
if errorlevel 1 exit /b 1
if "%GPU_TESTS%"=="1" (
 "%OUT%/msaa-vulkan.exe" "%OUT%/transfer"
 if errorlevel 1 exit /b 1
 set MSAA_DISABLE_SAMPLE_SHADING=1
 "%OUT%/msaa-vulkan.exe" "%OUT%/transfer"
 if errorlevel 1 exit /b 1
)
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. tools/tests/transfer_msaa_image_d3d12_test.c /Fo"%OUT%/" /Fe"%OUT%/msaa-warp.exe" /link d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 exit /b 1
"%OUT%/msaa-warp.exe" "%OUT%/transfer"
if errorlevel 1 exit /b 1
call tools\tests\run_transfer_uint_d3d12_bank_tests.cmd
exit /b %errorlevel%
