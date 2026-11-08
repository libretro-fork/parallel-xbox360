@echo off
setlocal
if "%~1"=="" exit /b 2
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined MEDIA_TEST_BUILD set MEDIA_TEST_BUILD=build\native-msvc
if not defined MEDIA_TEST_OUT set MEDIA_TEST_OUT=build\tests\native-media-c89
if not exist "%MEDIA_TEST_OUT%" mkdir "%MEDIA_TEST_OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include tools\tests\core_media_audio_test.c /Fo"%MEDIA_TEST_OUT%\test.obj" /Fe"%MEDIA_TEST_OUT%\test.exe" /link /LIBPATH:"%MEDIA_TEST_BUILD%\obj\Windows" xenia-xma-native.lib retro-common.lib winmm.lib ws2_32.lib advapi32.lib
if errorlevel 1 goto fail
:next
if "%~1"=="" goto done
"%MEDIA_TEST_OUT%\test.exe" "%~1"
if errorlevel 1 goto fail
shift
goto next
:done
popd
exit /b 0
:fail
popd
exit /b 1
