@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SHADER_CALL_OUT set SHADER_CALL_OUT=build\tests\shader-call-msvc
if not exist "%SHADER_CALL_OUT%" mkdir "%SHADER_CALL_OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_shader_call_test.c /Fo"%SHADER_CALL_OUT%\test.obj" /Fe"%SHADER_CALL_OUT%\test.exe"
if errorlevel 1 goto fail
"%SHADER_CALL_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
