@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\presenter-msvc
if not exist %OUT% mkdir %OUT%
set INC=/I. /Isrc /Ithird_party/libretro-common/include /Ithird_party
cl /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DRETRO_ATOMIC_FORCE_MSVC %INC% /c tools/tests/core_presenter_test.c /Fo%OUT%\driver.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /DHAVE_THREADS /DRETRO_ATOMIC_FORCE_MSVC %INC% /c third_party/libretro-common/rthreads/rthreads.c /Fo%OUT%\threads.obj
if errorlevel 1 goto fail
cl /nologo /utf-8 /MD /O2 /W4 /WX /wd4201 /EHsc /std:c++20 /DNDEBUG /DRETRO_ATOMIC_FORCE_MSVC %INC% src/xenia/ui/presenter.cc tools/tests/core_presenter_bridge.cc /Fo%OUT%\ %OUT%\driver.obj %OUT%\threads.obj /Fe%OUT%\test.exe /link winmm.lib
if errorlevel 1 goto fail
%OUT%\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
