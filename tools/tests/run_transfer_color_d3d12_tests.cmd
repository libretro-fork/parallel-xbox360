@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined COLOR_D3D12_OUT set COLOR_D3D12_OUT=build\tests\transfer-color-d3d12
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
set OUT=%COLOR_D3D12_OUT%
if not exist "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" exit /b 1
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=/nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/libretro-common/include /Isrc/xenia/gpu/dxil/bytecode %COLOR_D3D12_FLAGS%
cl %FLAGS% tools/tests/transfer_color_d3d12_bank_test.c libretro/core_transfer_color_d3d12.c /Fo"%OUT%/" /Fe"%OUT%/ownership.exe" /link /DEBUG
if errorlevel 1 exit /b 1
"%OUT%/ownership.exe"
if errorlevel 1 exit /b 1
set SOURCES=tools/tests/transfer_color_d3d12_init_test.c libretro/core_transfer_color_d3d12.c libretro/core_transfer_uint_d3d12.c src/xenia/gpu/dxil/transfer_uint.c
set LIBRARIES="%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /CGTHREADS:3 /DEBUG
cl %FLAGS% /DXE_COLOR_D3D12_IMAGE_TEST %SOURCES% /Fo"%OUT%/" /Fe"%OUT%/images.exe" /link %LIBRARIES%
if errorlevel 1 exit /b 1
"%OUT%/images.exe"
if errorlevel 1 exit /b 1
if defined COLOR_D3D12_HARDWARE (
 "%OUT%/images.exe" hardware
 if errorlevel 1 exit /b 1
)
cl %FLAGS% /DXE_COLOR_D3D12_IMAGE_TEST /DXE_COLOR_D3D12_BAD_PACKET %SOURCES% /Fo"%OUT%/" /Fe"%OUT%/wrong-address.exe" /link %LIBRARIES%
if errorlevel 1 exit /b 1
"%OUT%/wrong-address.exe" > "%OUT%/wrong-address.log" 2>&1
if not "%ERRORLEVEL%"=="1" exit /b 1
set NEGATIVE_LOG=%OUT:/=\%\wrong-address.log
findstr /c:"image mismatch" "%NEGATIVE_LOG%" >nul
if errorlevel 1 exit /b 1
echo PASS: prepared color bank ownership, native WARP images and wrong-address negative control
exit /b 0
