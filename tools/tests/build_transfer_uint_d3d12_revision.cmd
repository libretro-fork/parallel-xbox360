@echo off
rem Producer code and native archive must come from the selected parent revision.
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined TRANSFER_D3D12_PARENT_SOURCE exit /b 1
if not defined TRANSFER_D3D12_PARENT_BUILD exit /b 1
if not defined TRANSFER_D3D12_WARM_OUT exit /b 1
set PARENT=%TRANSFER_D3D12_PARENT_SOURCE%
set BUILD=%TRANSFER_D3D12_PARENT_BUILD%
set OUT=%TRANSFER_D3D12_WARM_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set EXTRA=
set BANK=
if exist "%PARENT%/libretro/core_transfer_uint_d3d12.c" (
 set EXTRA=/DXE_TEST_D3D12_PREPARED_SOURCE
 set BANK="%PARENT%/libretro/core_transfer_uint_d3d12.c"
)
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /LD %EXTRA% /I"%PARENT%" /Itools/tests /I"%PARENT%/third_party/libretro-common/include" tools/tests/transfer_uint_d3d12_revision.c %BANK% /Fo"%OUT%/" /Fe"%OUT%/parent.dll" /link "%BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /IMPLIB:"%OUT%/parent.lib"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /Itools/tests tools/tests/transfer_uint_d3d12_revision_test.c /Fo"%OUT%/" /Fe"%OUT%/abi-test.exe"
if errorlevel 1 exit /b 1
"%OUT%/abi-test.exe" "%OUT%/parent.dll"
exit /b %errorlevel%
