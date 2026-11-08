@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined XMA_CODEC_BUILD set XMA_CODEC_BUILD=build\windows-native
if not defined XMA_CHECKPOINT_OUT set XMA_CHECKPOINT_OUT=build\tests\xma-checkpoint-msvc
set OUT=%XMA_CHECKPOINT_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %XMA_CHECKPOINT_FLAGS% /nologo /TC /MD /O2 /W4 /WX /Ithird_party\libretro-common\include tools\tests\core_xma_checkpoint_test.c /Fo"%OUT%\driver.obj" /Fe"%OUT%\test.exe" /link /LIBPATH:"%XMA_CODEC_BUILD%\obj\Windows" retro-common.lib
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
