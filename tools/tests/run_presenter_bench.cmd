@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\presenter-bench-msvc
if defined TEST_BUILD_DIR set OUT=%TEST_BUILD_DIR%
if not exist %OUT% mkdir %OUT%
set SOURCE=src/xenia/ui/presenter.cc
set INC=/I. /Isrc /Ithird_party /Ithird_party/libretro-common/include /Itools/tests
set PARENT=
if not defined PRESENTER_REFERENCE_REVISION goto compile
if not exist %OUT%\reference\src\xenia\ui mkdir %OUT%\reference\src\xenia\ui
git show "%PRESENTER_REFERENCE_REVISION%:src/xenia/ui/presenter.h" > %OUT%\reference\src\xenia\ui\presenter.h
if errorlevel 1 goto fail
git show "%PRESENTER_REFERENCE_REVISION%:src/xenia/ui/presenter.cc" > %OUT%\reference\src\xenia\ui\presenter.cc
if errorlevel 1 goto fail
set SOURCE=%OUT%\reference\src\xenia\ui\presenter.cc
set INC=/I%OUT%/reference/src %INC%
set PARENT=/DXE_PRESENTER_PARENT
:compile
cl /nologo /TC /MD /O2 /W4 /WX %INC% /c tools/tests/core_presenter_bench.c /Fo%OUT%\driver.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 %INC% /c tools/tests/core_pipeline_bench_profile.c /Fo%OUT%\profile.obj
if errorlevel 1 goto fail
cl /nologo /utf-8 /MD /O2 /W4 /WX /wd4201 /EHsc /std:c++20 /DNDEBUG /DRETRO_ATOMIC_FORCE_MSVC %PARENT% %INC% %SOURCE% tools/tests/core_presenter_bench_bridge.cc /Fo%OUT%\ %OUT%\driver.obj %OUT%\profile.obj /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
