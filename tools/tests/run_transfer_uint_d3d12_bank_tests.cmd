@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined TRANSFER_D3D12_HEAD_SOURCE set TRANSFER_D3D12_HEAD_SOURCE=%ROOT%
set HEAD=%TRANSFER_D3D12_HEAD_SOURCE%
if not defined MSAA_IMAGE_OUT set MSAA_IMAGE_OUT=build\tests\transfer-msaa-image-msvc
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
set OUT=%MSAA_IMAGE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set INC=/I"%HEAD%" /Itools/tests /I"%HEAD%/third_party/libretro-common/include" /I"%HEAD%/src/xenia/gpu/dxil/bytecode"
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS %INC% tools/tests/transfer_uint_d3d12_bank_test.c "%HEAD%/libretro/core_transfer_uint_d3d12.c" "%HEAD%/src/xenia/gpu/dxil/transfer_uint.c" /Fo"%OUT%/" /Fe"%OUT%/uint-bank-ownership.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 exit /b 1
"%OUT%/uint-bank-ownership.exe"
if errorlevel 1 exit /b 1
cl /nologo /c /MD /O2 /EHsc /std:c++20 tools/tests/core_dxil_validation_bridge.cc /Fo"%OUT%/uint-bank-validator.obj"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS %INC% tools/tests/transfer_uint_production_d3d12_test.c "%HEAD%/libretro/core_transfer_uint_d3d12.c" "%HEAD%/src/xenia/gpu/dxil/transfer_uint.c" /Fo"%OUT%/" /Fe"%OUT%/uint-bank-image.exe" /link "%OUT%/uint-bank-validator.obj" "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 exit /b 1
if not defined XE_TEST_DXIL_VALIDATOR set XE_TEST_DXIL_VALIDATOR=%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll
"%OUT%/uint-bank-image.exe" "%OUT%/transfer"
exit /b %errorlevel%
