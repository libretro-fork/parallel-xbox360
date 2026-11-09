@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined MPSC_RECORDS_BUILD set MPSC_RECORDS_BUILD=build\windows-native
if not defined MPSC_RECORDS_OUT set MPSC_RECORDS_OUT=build\tests\mpsc-records-msvc
set OUT=%MPSC_RECORDS_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=%MPSC_RECORDS_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DXE_MPSC_RECORDS_TEST /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include
cl %FLAGS% /c libretro/core_mpsc_records.c /Fo"%OUT%\writer.obj" /Fd"%OUT%\writer.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /c tools/tests/core_mpsc_records_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 goto fail
cl %MPSC_RECORDS_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\writer.obj" "%MPSC_RECORDS_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
