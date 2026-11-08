@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SPV_BINDINGS_OUT set SPV_BINDINGS_OUT=build\tests\spv-bindings-msvc
if not exist "%SPV_BINDINGS_OUT%" mkdir "%SPV_BINDINGS_OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include tools\tests\core_spv_bindings_test.c /Fo"%SPV_BINDINGS_OUT%\test.obj" /Fe"%SPV_BINDINGS_OUT%\test.exe"
if errorlevel 1 goto fail
"%SPV_BINDINGS_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
