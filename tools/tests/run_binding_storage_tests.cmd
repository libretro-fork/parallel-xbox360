@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined BINDING_STORAGE_OUT set BINDING_STORAGE_OUT=build\tests\binding-storage-msvc
if not exist "%BINDING_STORAGE_OUT%" mkdir "%BINDING_STORAGE_OUT%"
cl %BINDING_STORAGE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. tools\tests\core_binding_storage_test.c /Fo"%BINDING_STORAGE_OUT%\test.obj" /Fd"%BINDING_STORAGE_OUT%\test.pdb" /Fe"%BINDING_STORAGE_OUT%\test.exe"
if errorlevel 1 goto fail
"%BINDING_STORAGE_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
