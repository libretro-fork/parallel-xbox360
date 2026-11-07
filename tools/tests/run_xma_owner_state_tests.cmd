@echo off
setlocal
if not defined XMA_INPUT_RAW exit /b 2
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined XMA_OWNER_BUILD set XMA_OWNER_BUILD=build\windows-native
if not defined XMA_OWNER_OUT set XMA_OWNER_OUT=build\tests\xma-owner-state-msvc
set OUT=%XMA_OWNER_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %XMA_OWNER_FLAGS% /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include /c tools\tests\core_xma_owner_state_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if not "%errorlevel%"=="0" goto fail
cl %XMA_OWNER_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /c tools\tests\xma_owner_state_bridge.cc /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if not "%errorlevel%"=="0" goto fail
type nul > "%OUT%\libs.rsp"
for %%L in ("%XMA_OWNER_BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
cl %XMA_OWNER_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\bridge.obj" %XMA_OWNER_OBJECT% "%XMA_OWNER_BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_events.c.obj" @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib xinput.lib comctl32.lib shcore.lib dxguid.lib d3d12.lib dxgi.lib synchronization.lib version.lib uuid.lib gdi32.lib imm32.lib setupapi.lib cfgmgr32.lib hid.lib dwmapi.lib comdlg32.lib oleaut32.lib /Fe"%OUT%\test.exe" /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
"%OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
