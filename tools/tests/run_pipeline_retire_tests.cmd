@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined PIPELINE_RETIRE_BUILD set PIPELINE_RETIRE_BUILD=build\windows-native
if not defined PIPELINE_RETIRE_OUT set PIPELINE_RETIRE_OUT=build\tests\pipeline-retire-msvc
set OUT=%PIPELINE_RETIRE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=%PIPELINE_RETIRE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include
cl %FLAGS% /c libretro/core_pipeline_retire.c /Fo"%OUT%\retire.obj" /Fd"%OUT%\retire.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /c tools/tests/core_pipeline_retire_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 goto fail
cl %PIPELINE_RETIRE_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\retire.obj" "%PIPELINE_RETIRE_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
