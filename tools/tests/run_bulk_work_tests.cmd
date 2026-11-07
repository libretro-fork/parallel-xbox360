@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined BULK_TEST_BUILD set BULK_TEST_BUILD=build\windows-native
if not defined BULK_TEST_OUT set BULK_TEST_OUT=build\tests\bulk-work-msvc
set OUT=%BULK_TEST_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=%BULK_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include
cl %FLAGS% /c libretro/core_bulk_work.c /Fo"%OUT%\real.obj" /Fd"%OUT%\real.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /FItools/tests/core_bulk_fault.h /c libretro/core_bulk_work.c /Fo"%OUT%\fault.obj" /Fd"%OUT%\fault.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /c libretro/core_gpu_worklist.c /Fo"%OUT%\storage.obj" /Fd"%OUT%\storage.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /c tools/tests/core_bulk_work_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 goto fail
cl %BULK_TEST_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\fault.obj" "%BULK_TEST_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
cl %BULK_TEST_FLAGS% /nologo /MD /O2 /EHsc /std:c++20 /W4 /WX /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include tools/tests/core_bulk_work_bridge.cc "%OUT%\real.obj" "%OUT%\storage.obj" "%BULK_TEST_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\bridge.exe" /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if errorlevel 1 goto fail
"%OUT%\bridge.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
