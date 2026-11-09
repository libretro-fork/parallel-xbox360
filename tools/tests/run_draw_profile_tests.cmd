@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if "%~2"=="" exit /b 2
if not exist build\tests\draw-profile-msvc mkdir build\tests\draw-profile-msvc
cl /nologo /TC /MD /O2 /W4 /WX /I. tools/tests/core_draw_profile_test.c /Fobuild\tests\draw-profile-msvc\test.obj /Febuild\tests\draw-profile-msvc\test.exe
if errorlevel 1 goto fail
build\tests\draw-profile-msvc\test.exe "%~f1" "%~2"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
