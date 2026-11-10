@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined UINT_IMAGE_OUT set UINT_IMAGE_OUT=build\tests\transfer-uint-image-msvc
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
set OUT=%UINT_IMAGE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /I. tools/tests/core_transfer_uint_packet_test.c /Fo"%OUT%/" /Fe"%OUT%/packet.exe"
if errorlevel 1 goto fail
"%OUT%/packet.exe"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/libretro-common/include /Ithird_party/SPIRV-Headers/include tools/tests/transfer_uint_spv_fixture.c libretro/core_transfer_uint_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/spv-generator.exe"
if errorlevel 1 goto fail
"%OUT%/spv-generator.exe" "%OUT%/transfer"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. /Isrc/xenia/gpu/dxil/bytecode tools/tests/transfer_uint_dxil_fixture.c src/xenia/gpu/dxil/transfer_uint.c src/xenia/gpu/dxil/transfer_sample.c /Fo"%OUT%/" /Fe"%OUT%/dxil-generator.exe" /link "%UINT_IMAGE_BUILD%\obj\Windows\xenia-native-dxil.lib"
if errorlevel 1 goto fail
"%OUT%/dxil-generator.exe" "%OUT%/transfer"
if errorlevel 1 goto fail
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/transfer.list"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party/Vulkan-Headers/include tools/tests/transfer_uint_image_vulkan_test.c /Fo"%OUT%/" /Fe"%OUT%/vulkan.exe"
if errorlevel 1 goto fail
if "%GPU_TESTS%"=="1" (
 "%OUT%/vulkan.exe" "%OUT%/transfer"
 if errorlevel 1 goto fail
)
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. tools/tests/transfer_uint_image_d3d12_test.c /Fo"%OUT%/" /Fe"%OUT%/warp.exe" /link d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 goto fail
"%OUT%/warp.exe" "%OUT%/transfer"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
