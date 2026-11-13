@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SHADER_LABELS_OUT set SHADER_LABELS_OUT=build\tests\shader-labels-msvc
if not exist "%SHADER_LABELS_OUT%" mkdir "%SHADER_LABELS_OUT%"
cl %SHADER_LABELS_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_shader_labels_test.c /Fo"%SHADER_LABELS_OUT%\test.obj" /Fe"%SHADER_LABELS_OUT%\test.exe"
if errorlevel 1 goto fail
"%SHADER_LABELS_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
