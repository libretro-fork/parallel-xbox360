@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined CACHE_WRITER_BUILD set CACHE_WRITER_BUILD=build\windows-native
if not defined CACHE_WRITER_OUT set CACHE_WRITER_OUT=build\tests\cache-writer-msvc
set OUT=%CACHE_WRITER_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=%CACHE_WRITER_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include
cl %FLAGS% /c libretro/core_cache_writer.c /Fo"%OUT%\writer.obj" /Fd"%OUT%\writer.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /c tools/tests/core_cache_writer_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 goto fail
cl %CACHE_WRITER_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\writer.obj" "%CACHE_WRITER_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
