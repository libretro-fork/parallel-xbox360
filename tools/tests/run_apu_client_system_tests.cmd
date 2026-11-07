@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined APU_SYSTEM_BUILD set APU_SYSTEM_BUILD=build\windows-native
if not defined APU_SYSTEM_OUT set APU_SYSTEM_OUT=build\tests\apu-client-system-msvc
set OUT=%APU_SYSTEM_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %APU_SYSTEM_FLAGS% /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include /c tools\tests\core_apu_client_system_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if not "%errorlevel%"=="0" goto fail
cl %APU_SYSTEM_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /Ithird_party\FFmpeg /Ithird_party\ffmpeg-xenia /c tools\tests\apu_client_system_bridge.cc /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if not "%errorlevel%"=="0" goto fail
type nul > "%OUT%\libs.rsp"
for %%L in ("%APU_SYSTEM_BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
cl %APU_SYSTEM_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\bridge.obj" %APU_SYSTEM_OBJECT% "%APU_SYSTEM_BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_events.c.obj" "%APU_SYSTEM_BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_disc.c.obj" @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib xinput.lib comctl32.lib shcore.lib dxguid.lib d3d12.lib dxgi.lib synchronization.lib version.lib uuid.lib gdi32.lib imm32.lib setupapi.lib cfgmgr32.lib hid.lib dwmapi.lib comdlg32.lib oleaut32.lib /Fe"%OUT%\test.exe" /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
"%OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
