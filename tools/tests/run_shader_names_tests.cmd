@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SHADER_NAMES_OUT set SHADER_NAMES_OUT=build\tests\shader-names-msvc
if not exist "%SHADER_NAMES_OUT%" mkdir "%SHADER_NAMES_OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include tools\tests\core_shader_names_test.c /Fo"%SHADER_NAMES_OUT%\test.obj" /Fe"%SHADER_NAMES_OUT%\test.exe"
if errorlevel 1 goto fail
"%SHADER_NAMES_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
