@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined TRANSFER_REVISION_OUT set TRANSFER_REVISION_OUT=build\tests\transfer-revision-msvc
for %%O in ("%TRANSFER_REVISION_OUT%") do set OUT=%%~fO
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /LD /I. /Itools/tests /Ithird_party/Vulkan-Headers/include /Ithird_party/SPIRV-Headers/include tools/tests/transfer_uint_revision.c libretro/core_transfer_uint_vk.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/parent.dll" /link /IMPLIB:"%OUT%/parent.lib"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /Itools/tests /Ithird_party/Vulkan-Headers/include tools/tests/transfer_uint_revision_test.c /Fo"%OUT%/" /Fe"%OUT%/abi-test.exe"
if errorlevel 1 exit /b 1
"%OUT%/abi-test.exe" "%OUT%/parent.dll"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DXE_SHADER_COMPILE_BENCH /I. /Itools/tests /Ithird_party/Vulkan-Headers/include /Ithird_party/SPIRV-Headers/include tools/tests/transfer_uint_warm_vulkan_test.c libretro/core_transfer_uint_vk.c libretro/core_transfer_uint_spv.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/warm.exe"
exit /b %errorlevel%
