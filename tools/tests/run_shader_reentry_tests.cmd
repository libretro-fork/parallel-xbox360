@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SHADER_REENTRY_OUT set SHADER_REENTRY_OUT=build\tests\shader-reentry-msvc
if not exist "%SHADER_REENTRY_OUT%" mkdir "%SHADER_REENTRY_OUT%"
cl %SHADER_REENTRY_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_shader_reentry_test.c /Fo"%SHADER_REENTRY_OUT%\test.obj" /Fe"%SHADER_REENTRY_OUT%\test.exe"
if errorlevel 1 goto fail
"%SHADER_REENTRY_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
