@echo off
rem Compile the hardware-only warmed transfer harness; GPU execution is explicit.
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined TRANSFER_D3D12_HEAD_SOURCE set TRANSFER_D3D12_HEAD_SOURCE=%ROOT%
set HEAD=%TRANSFER_D3D12_HEAD_SOURCE%
if not exist "%HEAD%/libretro/core_transfer_uint_d3d12.c" exit /b 1
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
if not defined TRANSFER_D3D12_WARM_OUT set TRANSFER_D3D12_WARM_OUT=build\tests\transfer-warm-d3d12
set OUT=%TRANSFER_D3D12_WARM_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /c /MD /O2 /EHsc /std:c++20 tools/tests/core_dxil_validation_bridge.cc /Fo"%OUT%/validator.obj"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I"%HEAD%" /Itools/tests /I"%HEAD%/third_party/libretro-common/include" /I"%HEAD%/src/xenia/gpu/dxil/bytecode" tools/tests/transfer_uint_warm_d3d12_test.c "%HEAD%/libretro/core_transfer_uint_d3d12.c" "%HEAD%/src/xenia/gpu/dxil/transfer_uint.c" /Fo"%OUT%/" /Fe"%OUT%/warm-test.exe" /link "%OUT%/validator.obj" "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib
exit /b %errorlevel%
