@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined LAYOUT_READY_BUILD set LAYOUT_READY_BUILD=build\windows-native
set OUT=build\tests\layout-ready-msvc
if not exist "%OUT%" mkdir "%OUT%"
cl %LAYOUT_READY_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party/libretro-common/include /external:Ithird_party/Vulkan-Headers/include /external:W0 tools/tests/core_layout_ready_test.c /Fo"%OUT%\test.obj" /Fe"%OUT%\test.exe" /link "%LAYOUT_READY_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party/libretro-common/include /external:Ithird_party/Vulkan-Headers/include /external:W0 tools/tests/core_layout_device_test.c /Fo"%OUT%\device.obj" /Fe"%OUT%\device.exe"
if errorlevel 1 goto fail
"%OUT%\device.exe"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
