@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined UINT_BANK_OUT set UINT_BANK_OUT=build\tests\transfer-uint-bank-msvc
set OUT=%UINT_BANK_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /DXE_GPU_DRAW_STATS=1 /I. /Ithird_party/libretro-common/include /Ithird_party/Vulkan-Headers/include /Ithird_party/SPIRV-Headers/include tools/tests/core_transfer_uint_vk_test.c libretro/core_draw_stats.c libretro/core_transfer_uint_vk.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/bank.exe"
if errorlevel 1 exit /b 1
"%OUT%/bank.exe"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/libretro-common/include /Ithird_party/SPIRV-Headers/include tools/tests/transfer_uint_shape_spv_fixture.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/shape-generator.exe"
if errorlevel 1 exit /b 1
"%OUT%/shape-generator.exe" "%OUT%/shape"
if errorlevel 1 exit /b 1
call tools\tests\run_command_stream_tests.cmd
exit /b %errorlevel%
