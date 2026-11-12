@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined IDENTITY_OUT set IDENTITY_OUT=build\tests\transfer-identity-msvc
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
if not defined IDENTITY_REFERENCE_BUILD set IDENTITY_REFERENCE_BUILD=%UINT_IMAGE_BUILD%
if not defined PYTHON set PYTHON=python
set "IDENTITY_LEGACY_PREFIX="
set "IDENTITY_FIRST_SKIP_DRAW="
set OUT=%IDENTITY_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=/nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party/libretro-common/include /Ithird_party/SPIRV-Headers/include /Ithird_party/Vulkan-Headers/include /Isrc/xenia/gpu/dxil/bytecode
set SPV=libretro/core_transfer_identity_vk.c libretro/core_transfer_identity_spv.c libretro/core_spv_binary.c
set DXIL=libretro/core_transfer_identity_d3d12.c libretro/core_transfer_uint_d3d12.c
cl %FLAGS% tools/tests/core_transfer_identity_test.c /Fo"%OUT%/" /Fe"%OUT%/packet.exe"
if errorlevel 1 exit /b 1
"%OUT%/packet.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% /DXE_GPU_DRAW_STATS=1 tools/tests/core_transfer_identity_vk_test.c libretro/core_draw_stats.c %SPV% /Fo"%OUT%/" /Fe"%OUT%/bank.exe"
if errorlevel 1 exit /b 1
"%OUT%/bank.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_d3d12_bank_test.c %DXIL% /Fo"%OUT%/" /Fe"%OUT%/d3d12-bank.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxguid.lib /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/d3d12-bank.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_fixture.c libretro/core_transfer_identity_spv.c libretro/core_transfer_geometry.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/spv-generator.exe"
if errorlevel 1 exit /b 1
"%OUT%/spv-generator.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_msaa_spv_fixture.c libretro/core_spv_binary.c /Fo"%OUT%/" /Fe"%OUT%/msaa-spv.exe"
if errorlevel 1 exit /b 1
"%OUT%/msaa-spv.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
cl %FLAGS% /DIDENTITY_DXIL tools/tests/transfer_identity_fixture.c src/xenia/gpu/dxil/transfer_identity.c src/xenia/gpu/dxil/transfer_sample.c /Fo"%OUT%/" /Fe"%OUT%/dxil-generator.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/dxil-generator.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_msaa_dxil_fixture.c /Fo"%OUT%/" /Fe"%OUT%/msaa-dxil.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/msaa-dxil.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/identity-dxil.list"
if errorlevel 1 exit /b 1
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/identity-msaa.list"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_reference_dxil_fixture.c /Fo"%OUT%/" /Fe"%OUT%/reference-dxil.exe" /link "%IDENTITY_REFERENCE_BUILD%/obj/Windows/xenia-native-dxil.lib" /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/reference-dxil.exe" "%OUT%/legacy"
if errorlevel 1 exit /b 1
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/legacy-reference.list"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_msaa_reference_dxil_fixture.c /Fo"%OUT%/" /Fe"%OUT%/msaa-reference-dxil.exe" /link "%IDENTITY_REFERENCE_BUILD%/obj/Windows/xenia-native-dxil.lib" /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/msaa-reference-dxil.exe" "%OUT%/msaa-legacy" --all-sources
if errorlevel 1 exit /b 1
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/msaa-legacy-reference.list"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_image_d3d12_test.c %DXIL% /Fo"%OUT%/" /Fe"%OUT%/image-d3d12.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/image-d3d12.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
set "IDENTITY_LEGACY_PREFIX=%OUT%/legacy"
"%OUT%/image-d3d12.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
set "IDENTITY_LEGACY_PREFIX="
cl %FLAGS% tools/tests/transfer_identity_msaa_d3d12_test.c %DXIL% /Fo"%OUT%/" /Fe"%OUT%/msaa-d3d12.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /CGTHREADS:3
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_check.py d3d12 "%OUT%/msaa-d3d12.exe" "%OUT%/identity" "%OUT%/d3d12-logs"
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_check.py d3d12 "%OUT%/msaa-d3d12.exe" "%OUT%/identity" "%OUT%/d3d12-typed-logs" --typed
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_msaa_reference_d3d12_test.c %DXIL% /Fo"%OUT%/" /Fe"%OUT%/msaa-parent-d3d12.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /CGTHREADS:3
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_reference_d3d12_check.py "%OUT%/msaa-parent-d3d12.exe" "%OUT%/identity" "%OUT%/msaa-legacy" "%OUT%/d3d12-parent-logs"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_msaa_warm_dxil_fixture.c /Fo"%OUT%/" /Fe"%OUT%/msaa-warm-utilities.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/msaa-warm-utilities.exe" "%OUT%/warm"
if errorlevel 1 exit /b 1
call tools\tests\run_dxil_validation_tests.cmd "%WindowsSdkDir%bin\%WindowsSDKVersion%x64\dxil.dll" "%OUT%/warm-msaa.list"
if errorlevel 1 exit /b 1
cl %FLAGS% /DXE_SHADER_COMPILE_BENCH=1 /DXE_IDENTITY_MSAA_SMALL=1 tools/tests/transfer_identity_msaa_warm_d3d12_test.c %DXIL% /Fo"%OUT%/" /Fe"%OUT%/msaa-work-d3d12.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /CGTHREADS:3
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_warm_d3d12_check.py "%OUT%/msaa-work-d3d12.exe" "%OUT%/warm" "%OUT%/msaa-legacy" "%OUT%/d3d12-work-logs" --diagnostic
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_warm_d3d12_check.py "%OUT%/msaa-work-d3d12.exe" "%OUT%/warm" "%OUT%/msaa-legacy" "%OUT%/d3d12-stale-logs" --diagnostic --control stale
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_warm_d3d12_check.py "%OUT%/msaa-work-d3d12.exe" "%OUT%/warm" "%OUT%/msaa-legacy" "%OUT%/d3d12-sample-logs" --diagnostic --control sample
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_bank_init_d3d12_test.c %DXIL% /Fo"%OUT%/" /Fe"%OUT%/bank-init-d3d12.exe" /link "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib psapi.lib /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/bank-init-d3d12.exe" "%OUT%/identity" 1 1 1
if errorlevel 1 exit /b 1
"%OUT%/bank-init-d3d12.exe" "%OUT%/identity" 4 4 1
if errorlevel 1 exit /b 1
"%OUT%/bank-init-d3d12.exe" "%OUT%/identity" 1 1 0
if errorlevel 1 exit /b 1
set "IDENTITY_FIRST_SKIP_DRAW=1"
"%OUT%/bank-init-d3d12.exe" "%OUT%/identity" 1 1 1 >"%OUT%/bank-init-first-skip.log" 2>&1
if not errorlevel 1 exit /b 1
set "IDENTITY_FIRST_SKIP_DRAW="
findstr /c:"First transfer image mismatch" <"%OUT%/bank-init-first-skip.log" >nul
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_image_vulkan_test.c %SPV% /Fo"%OUT%/" /Fe"%OUT%/image-vulkan.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_msaa_vulkan_test.c %SPV% /Fo"%OUT%/" /Fe"%OUT%/msaa-vulkan.exe"
if errorlevel 1 exit /b 1
cl %FLAGS% tools/tests/transfer_identity_bank_init_vulkan_test.c %SPV% /Fo"%OUT%/" /Fe"%OUT%/bank-init-vulkan.exe"
if errorlevel 1 exit /b 1
if not "%GPU_TESTS%"=="1" (
 echo UNVERIFIED: identity Vulkan images disabled ^(set GPU_TESTS=1^)
 exit /b 0
)
"%OUT%/image-vulkan.exe" "%OUT%/identity"
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_check.py vulkan "%OUT%/msaa-vulkan.exe" "%OUT%/identity" "%OUT%/vulkan-logs"
if errorlevel 1 exit /b 1
"%PYTHON%" tools/tests/transfer_identity_msaa_check.py vulkan "%OUT%/msaa-vulkan.exe" "%OUT%/identity" "%OUT%/vulkan-typed-logs" --typed
if errorlevel 1 exit /b 1
"%OUT%/bank-init-vulkan.exe" "%OUT%/identity" 1 1 0 1 0
if errorlevel 1 exit /b 1
"%OUT%/bank-init-vulkan.exe" "%OUT%/identity" 4 4 1 1 0
if errorlevel 1 exit /b 1
"%OUT%/bank-init-vulkan.exe" "%OUT%/identity" 4 4 1 0 0
if errorlevel 1 exit /b 1
"%OUT%/bank-init-vulkan.exe" "%OUT%/identity" 4 4 1 1 1
if errorlevel 1 exit /b 1
"%OUT%/bank-init-vulkan.exe" "%OUT%/identity" 4 4 1 0 1
exit /b %errorlevel%
