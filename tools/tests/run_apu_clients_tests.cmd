@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined APU_CLIENTS_BUILD set APU_CLIENTS_BUILD=build\windows-native
if not defined APU_CLIENTS_OUT set APU_CLIENTS_OUT=build\tests\apu-clients-msvc
set OUT=%APU_CLIENTS_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %APU_CLIENTS_FLAGS% /nologo /TC /O2 /MD /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /DXE_APU_CLIENT_TEST /Dcpu_features_get_time_usec=xe_apu_test_time /I. /Ithird_party\libretro-common\include /c libretro\core_apu_clients.c /Fo"%OUT%\clients.obj" /Fd"%OUT%\clients.pdb"
if not "%errorlevel%"=="0" goto fail
cl %APU_CLIENTS_FLAGS% /nologo /TC /O2 /MD /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /DXE_APU_CLIENT_TEST /I. /Ithird_party\libretro-common\include /c tools\tests\core_apu_clients_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if not "%errorlevel%"=="0" goto fail
cl %APU_CLIENTS_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\clients.obj" /Fe"%OUT%\test.exe" /link "%APU_CLIENTS_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib
if not "%errorlevel%"=="0" goto fail
"%OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
