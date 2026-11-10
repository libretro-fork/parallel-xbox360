@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined TRANSFER_OUT set TRANSFER_OUT=build\tests\transfer-geometry-msvc
if not defined TRANSFER_BUILD set TRANSFER_BUILD=build\windows-native
set OUT=%TRANSFER_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /DXE_TRANSFER_TEST_DXIL /I. /Ithird_party\libretro-common\include /Ithird_party\SPIRV-Headers\include tools\tests\core_transfer_geometry_test.c libretro\core_transfer_geometry.c libretro\core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%\generator.exe" /link "%TRANSFER_BUILD%\obj\Windows\xenia-native-dxil.lib"
if errorlevel 1 goto fail
"%OUT%\generator.exe" "%OUT%\transfer.spv"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include tools\tests\transfer_geometry_d3d12_test.c /Fo"%OUT%/" /Fe"%OUT%\warp.exe" /link d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 goto fail
"%OUT%\warp.exe" "%OUT%\transfer.spv.dxil"
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /I. /Ithird_party\libretro-common\include /Ithird_party\Vulkan-Headers\include tools\tests\transfer_geometry_vulkan_test.c /Fo"%OUT%/" /Fe"%OUT%\vulkan.exe"
if errorlevel 1 goto fail
"%OUT%\vulkan.exe" "%OUT%\transfer.spv.xfb.spv"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
