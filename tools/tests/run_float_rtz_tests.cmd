@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined FLOAT_RTZ_OUT set FLOAT_RTZ_OUT=build\tests\float-rtz-msvc
if not exist "%FLOAT_RTZ_OUT%" mkdir "%FLOAT_RTZ_OUT%"
cl %FLOAT_RTZ_FLAGS% /nologo /TC /MD /O2 /fp:strict /W4 /WX /I. tools\tests\core_float_rtz_test.c /Fo"%FLOAT_RTZ_OUT%\test.obj" /Fd"%FLOAT_RTZ_OUT%\test.pdb" /Fe"%FLOAT_RTZ_OUT%\test.exe"
if errorlevel 1 goto fail
"%FLOAT_RTZ_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
