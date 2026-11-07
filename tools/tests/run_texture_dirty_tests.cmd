@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined TEXTURE_DIRTY_BUILD set TEXTURE_DIRTY_BUILD=build\windows-native
if not defined TEXTURE_DIRTY_OUT set TEXTURE_DIRTY_OUT=build\tests\texture-dirty-msvc
set OUT=%TEXTURE_DIRTY_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %TEXTURE_DIRTY_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include /c tools\tests\core_texture_dirty_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 goto fail
cl %TEXTURE_DIRTY_FLAGS% /nologo /MD "%OUT%\driver.obj" "%TEXTURE_DIRTY_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
