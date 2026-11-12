@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined PRIMITIVE_RANGES_OUT set PRIMITIVE_RANGES_OUT=build\tests\primitive-ranges-msvc
if not exist "%PRIMITIVE_RANGES_OUT%" mkdir "%PRIMITIVE_RANGES_OUT%"
cl /nologo /TC /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_primitive_ranges_test.c /Fo"%PRIMITIVE_RANGES_OUT%\test.obj" /Fe"%PRIMITIVE_RANGES_OUT%\test.exe"
if errorlevel 1 goto fail
"%PRIMITIVE_RANGES_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
