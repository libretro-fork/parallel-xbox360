@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SAMPLE_OUT set SAMPLE_OUT=build\tests\transfer-sample-msvc
if not defined SAMPLE_BUILD set SAMPLE_BUILD=build\windows-native
set OUT=%SAMPLE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /I. tools\tests\core_transfer_sample_test.c /Fo"%OUT%/" /Fe"%OUT%\unit.exe"
if errorlevel 1 goto fail
"%OUT%\unit.exe"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. /Isrc\xenia\gpu\dxil\bytecode tools\tests\transfer_sample_dxil_fixture.c src\xenia\gpu\dxil\transfer_sample.c /Fo"%OUT%/" /Fe"%OUT%\dxil-generator.exe" /link "%SAMPLE_BUILD%\obj\Windows\xenia-native-dxil.lib"
if errorlevel 1 goto fail
"%OUT%\dxil-generator.exe" "%OUT%\sample.dxil"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. tools\tests\transfer_sample_d3d12_test.c /Fo"%OUT%/" /Fe"%OUT%\warp.exe" /link d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 goto fail
"%OUT%\warp.exe" "%OUT%\sample.dxil"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DXE_TRANSFER_SAMPLE_GENERATOR /I. /Ithird_party\SPIRV-Headers\include tools\tests\transfer_sample_spv_fixture.c libretro\core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%\generator.exe"
if errorlevel 1 goto fail
"%OUT%\generator.exe" "%OUT%\sample.spv"
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\Vulkan-Headers\include tools\tests\transfer_sample_vulkan_test.c /Fo"%OUT%/" /Fe"%OUT%\vulkan.exe"
if errorlevel 1 goto fail
"%OUT%\vulkan.exe" "%OUT%\sample.spv"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
