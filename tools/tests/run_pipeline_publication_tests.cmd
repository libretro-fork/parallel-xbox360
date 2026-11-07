@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined PIPELINE_PUB_BUILD set PIPELINE_PUB_BUILD=build\windows-native
if not defined PIPELINE_PUB_OUT set PIPELINE_PUB_OUT=build\tests\pipeline-publication-msvc
set OUT=%PIPELINE_PUB_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %PIPELINE_PUB_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include /c tools\tests\core_pipeline_publication_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 goto fail
cl %PIPELINE_PUB_FLAGS% /nologo /MD "%OUT%\driver.obj" "%PIPELINE_PUB_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
