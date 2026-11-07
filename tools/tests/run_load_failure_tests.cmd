@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined LOAD_FAILURE_BUILD set LOAD_FAILURE_BUILD=build\windows-native
if not defined LOAD_FAILURE_OUT set LOAD_FAILURE_OUT=build\tests\load-failure-msvc
set OUT=%LOAD_FAILURE_OUT%
if not defined LOAD_FAILURE_BRIDGE set LOAD_FAILURE_BRIDGE=tools\tests\load_failure_bridge.cc
if not exist "%OUT%" mkdir "%OUT%"
cl %LOAD_FAILURE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DXE_LOAD_NATIVE /D_CRT_SECURE_NO_WARNINGS /I. /Ilibretro /Ithird_party\libretro-common\include /Ithird_party\Vulkan-Headers\include /c tools\tests\core_load_failure_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if not "%errorlevel%"=="0" goto fail
cl %LOAD_FAILURE_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /Ilibretro /Ithird_party /Ithird_party\Vulkan-Headers\include /Ithird_party\FFmpeg /Ithird_party\ffmpeg-xenia /DXBYAK_NO_OP_NAMES /DXBYAK_ENABLE_OMITTED_OPERAND /DVULKAN_HPP_NO_TO_STRING /c "%LOAD_FAILURE_BRIDGE%" /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if not "%errorlevel%"=="0" goto fail
type nul > "%OUT%\libs.rsp"
for %%L in ("%LOAD_FAILURE_BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
for %%O in ("%LOAD_FAILURE_BUILD%\libretro\CMakeFiles\xenia-libretro.dir\*.obj") do if /I not "%%~nxO"=="libretro.cpp.obj" echo "%%~fO">> "%OUT%\libs.rsp"
cl %LOAD_FAILURE_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\bridge.obj" %LOAD_FAILURE_OBJECT% @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib xinput.lib comctl32.lib shcore.lib dxguid.lib d3d12.lib dxgi.lib synchronization.lib version.lib uuid.lib gdi32.lib imm32.lib setupapi.lib cfgmgr32.lib hid.lib dwmapi.lib comdlg32.lib oleaut32.lib /Fe"%OUT%\test.exe" /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
if not exist "%OUT%\system" mkdir "%OUT%\system"
"%OUT%\test.exe" "%OUT%\system"
if not "%errorlevel%"=="0" goto fail
set "VK_DRIVER_FILES=%CD%\%OUT%\missing-driver.json"
set "VK_ICD_FILENAMES=%VK_DRIVER_FILES%"
"%OUT%\test.exe" "%OUT%\system" providers
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
