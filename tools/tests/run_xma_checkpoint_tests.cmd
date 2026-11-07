@echo off
setlocal
rem Build the libavcodec/libavutil CMake targets first; no emulator is launched.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined XMA_CODEC_BUILD set XMA_CODEC_BUILD=build\windows-native
if not defined XMA_CHECKPOINT_OUT set XMA_CHECKPOINT_OUT=build\tests\xma-checkpoint-msvc
set OUT=%XMA_CHECKPOINT_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %XMA_CHECKPOINT_FLAGS% /nologo /TC /MD /O2 /W4 /WX /Ithird_party\FFmpeg /c tools\tests\core_xma_checkpoint_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if not "%errorlevel%"=="0" goto fail
cl %XMA_CHECKPOINT_FLAGS% /nologo /TP /MD /O2 /EHsc /std:c++20 /Ithird_party\ffmpeg-xenia /Ithird_party\FFmpeg /c tools\tests\xma_checkpoint_bridge.cc /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if not "%errorlevel%"=="0" goto fail
cl %XMA_CHECKPOINT_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\bridge.obj" %XMA_CHECKPOINT_DECODER_OBJECT% "%XMA_CODEC_BUILD%\obj\Windows\libavcodec.lib" "%XMA_CODEC_BUILD%\obj\Windows\libavutil.lib" bcrypt.lib /Fe"%OUT%\test.exe" /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
"%OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
