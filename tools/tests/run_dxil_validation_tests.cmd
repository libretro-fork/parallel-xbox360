@echo off
rem Optional: run from an x64 VS developer prompt with Windows SDK dxcapi.h.
rem Arguments: path to Microsoft's dxil.dll and path-list text file.
setlocal
if "%~2"=="" exit /b 2
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\dxil-validation-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /c tools/tests/core_dxil_validation_test.c /Fo%OUT%\test.obj
if errorlevel 1 goto fail
cl /nologo /MD /O2 /W4 /EHsc /std:c++20 /c tools/tests/core_dxil_validation_bridge.cc /Fo%OUT%\bridge.obj
if errorlevel 1 goto fail
cl /nologo /MD %OUT%\test.obj %OUT%\bridge.obj /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe "%~1" "%~2"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
