@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined XMA_PAYLOAD_OUT set XMA_PAYLOAD_OUT=build\tests\xma-payload-msvc
if not exist "%XMA_PAYLOAD_OUT%" mkdir "%XMA_PAYLOAD_OUT%"
cl %XMA_PAYLOAD_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_xma_payload_test.c /Fo"%XMA_PAYLOAD_OUT%\test.obj" /Fd"%XMA_PAYLOAD_OUT%\test.pdb" /Fe"%XMA_PAYLOAD_OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
"%XMA_PAYLOAD_OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
