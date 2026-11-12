@echo off
rem Test-only revision pair build. Requires an MSVC developer environment.
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined TRANSFER_WARM_PARENT_SOURCE exit /b 1
if not defined TRANSFER_WARM_HEAD_SOURCE exit /b 1
if not defined TRANSFER_WARM_OUT exit /b 1
for %%O in ("%TRANSFER_WARM_OUT%") do set OUT=%%~fO
set PARENT=%TRANSFER_WARM_PARENT_SOURCE%
set HEAD=%TRANSFER_WARM_HEAD_SOURCE%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /LD /I"%PARENT%" /Itools/tests /I"%PARENT%/third_party/Vulkan-Headers/include" /I"%PARENT%/third_party/SPIRV-Headers/include" tools/tests/transfer_uint_revision.c "%PARENT%/libretro/core_transfer_uint_vk.c" "%PARENT%/libretro/core_transfer_uint_spv.c" "%PARENT%/libretro/core_spv_binary.c" /Fo"%OUT%/" /Fe"%OUT%/parent.dll" /link /IMPLIB:"%OUT%/parent.lib"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /Itools/tests /Ithird_party/Vulkan-Headers/include tools/tests/transfer_uint_revision_test.c /Fo"%OUT%/" /Fe"%OUT%/abi-test.exe"
if errorlevel 1 exit /b 1
"%OUT%/abi-test.exe" "%OUT%/parent.dll"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/libretro-common/include /Ithird_party/SPIRV-Headers/include tools/tests/transfer_uint_warm_spv_fixture.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/probe-generator.exe"
if errorlevel 1 exit /b 1
"%OUT%/probe-generator.exe" "%OUT%/transfer"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DXE_SHADER_COMPILE_BENCH /I"%HEAD%" /Itools/tests /I. /I"%HEAD%/third_party/Vulkan-Headers/include" /I"%HEAD%/third_party/SPIRV-Headers/include" tools/tests/transfer_uint_warm_vulkan_test.c "%HEAD%/libretro/core_transfer_uint_vk.c" "%HEAD%/libretro/core_transfer_uint_spv.c" "%HEAD%/libretro/core_spv_binary.c" /Fo"%OUT%/" /Fe"%OUT%/transfer-warm.exe"
exit /b %errorlevel%
