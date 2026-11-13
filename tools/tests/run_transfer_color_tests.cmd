@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined COLOR_OUT set COLOR_OUT=build\tests\transfer-color-msvc
set OUT=%COLOR_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=/nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/libretro-common/include /Ithird_party/SPIRV-Headers/include /Ithird_party/Vulkan-Headers/include
for %%N in (color_packet format) do (
 cl %FLAGS% tools/tests/core_transfer_%%N_test.c /Fo"%OUT%/" /Fe"%OUT%/%%N.exe"
 if errorlevel 1 exit /b 1
 "%OUT%/%%N.exe"
 if errorlevel 1 exit /b 1
)
cl %FLAGS% tools/tests/core_transfer_color_vk_test.c libretro/core_transfer_color_vk.c libretro/core_transfer_color_spv.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/bank.exe"
if errorlevel 1 exit /b 1
"%OUT%/bank.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_color_spv_fixture.c /Fo"%OUT%/" /Fe"%OUT%/generate.exe"
if errorlevel 1 exit /b 1
"%OUT%/generate.exe" "%OUT%"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_color_shape_fixture.c /Fo"%OUT%/" /Fe"%OUT%/shape.exe"
if errorlevel 1 exit /b 1
"%OUT%/shape.exe" "%OUT%"
if errorlevel 1 exit /b 1
cl %FLAGS% /DXE_COLOR_WARM_BENCH tools/tests/transfer_color_image_vulkan_test.c libretro/core_transfer_color_vk.c libretro/core_transfer_color_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/warm.exe"
if errorlevel 1 exit /b 1
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
cl %FLAGS% /wd4201 /DXE_DXIL_EDRAM_TEST /Itools/tests /Isrc/xenia/gpu/dxil /Isrc/xenia/gpu/dxil/bytecode tools/tests/dxil_writer_test.c tools/tests/transfer_format_dxil_fixture.c tools/tests/dxil_alu_test.c src/xenia/gpu/dxil/transfer_format.c /Fo"%OUT%/" /Fe"%OUT%/format-dxil.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib"
if errorlevel 1 exit /b 1
"%OUT%/format-dxil.exe" "%OUT%/format.dxil" edram
if errorlevel 1 exit /b 1
echo %OUT%/format.dxil> "%OUT%/format.list"
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/format.list"
if errorlevel 1 exit /b 1
cl %FLAGS% /wd4201 tools/tests/dxil_runtime_test.c /Fo"%OUT%/" /Fe"%OUT%/format-runtime.exe" /link d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 exit /b 1
set XE_DXIL_TEST_HARDWARE=
"%OUT%/format-runtime.exe" "%OUT%/format.dxil" "%OUT%/format.dxil.expected" raw
if errorlevel 1 exit /b 1
exit /b 0
