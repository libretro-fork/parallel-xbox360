@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined READY_OWNED_BUILD set READY_OWNED_BUILD=build\windows-native
set OUT=build\tests\geometry-owned-msvc
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party/libretro-common/include /Isrc/xenia/gpu/dxil tools/tests/core_geometry_owned_test.c /Fo"%OUT%\test.obj" /Fe"%OUT%\test.exe" /link "%READY_OWNED_BUILD%\obj\Windows\xenia-native-dxil.lib" "%READY_OWNED_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
