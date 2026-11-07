@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined RESOLVE_ADDRESS_OUT set RESOLVE_ADDRESS_OUT=build\tests\resolve-address-msvc
set OUT=%RESOLVE_ADDRESS_OUT%
if not exist %OUT% mkdir %OUT%
set INC=/I. /Isrc /Ilibretro /Ithird_party/libretro-common/include /Ithird_party/fmt/include
cl %RESOLVE_ADDRESS_FLAGS% /nologo /TC /MD /O2 /W4 /WX %INC% /c tools/tests/core_resolve_address_test.c /Fd%OUT%\test.pdb /Fo%OUT%\test.obj
if errorlevel 1 goto fail
cl %RESOLVE_ADDRESS_FLAGS% /nologo /MD /O2 /W4 /WX /EHsc /std:c++20 /DNDEBUG %INC% /c tools/tests/core_resolve_address_bridge.cc /Fd%OUT%\bridge.pdb /Fo%OUT%\bridge.obj
if errorlevel 1 goto fail
cl %RESOLVE_ADDRESS_FLAGS% /nologo /MD %OUT%\test.obj %OUT%\bridge.obj /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
