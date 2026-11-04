@echo off
setlocal
rem Run from an x64 Native Tools command prompt after building xenia-libretro.
pushd "%~dp0..\.."
if not defined FIBER_TEST_LIBDIR set "FIBER_TEST_LIBDIR=build\cmake-msvc-native\obj\Windows"
set "OUT=build\tests\fiber-benchmark"
if not exist "%OUT%" mkdir "%OUT%"
git show 967d10598afc0a7b2f9d33f770efd528457756f9:src/xenia/base/threading_fiber.cc > "%OUT%\reference.cc"
if errorlevel 1 goto fail
for %%N in (make jump) do (
 git show 967d10598afc0a7b2f9d33f770efd528457756f9:third_party/boost_context/context/src/asm/%%N_x86_64_ms_pe_masm.asm > "%OUT%\%%N.asm"
 if errorlevel 1 goto fail
 ml64 /nologo /c /DBOOST_CONTEXT_EXPORT= /Fo"%OUT%\%%N.obj" "%OUT%\%%N.asm"
 if errorlevel 1 goto fail
)
set "INC=/I. /Isrc /Ithird_party/libretro-common/include /Ithird_party/fmt/include"
cl /nologo /MD /O2 /DNDEBUG /std:c++20 /utf-8 /EHsc /arch:AVX %INC% /c "%OUT%\reference.cc" /Fo"%OUT%\reference.obj"
if errorlevel 1 goto fail
cl /nologo /MD /O2 /DNDEBUG /std:c++20 /utf-8 /EHsc /arch:AVX %INC% /c tools/tests/core_fiber_bridge.cc /Fo"%OUT%\bridge.obj"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /c tools/tests/core_fiber_benchmark.c /Fo"%OUT%\main.obj"
if errorlevel 1 goto fail
set "LIBS=xenia-base.lib retro-common.lib fmt.lib winmm.lib bcrypt.lib ws2_32.lib iphlpapi.lib dbghelp.lib user32.lib shell32.lib advapi32.lib shlwapi.lib ole32.lib psapi.lib"
link /nologo /out:"%OUT%\fiber-workload-boost.exe" "%OUT%\main.obj" "%OUT%\bridge.obj" "%OUT%\reference.obj" "%OUT%\make.obj" "%OUT%\jump.obj" /LIBPATH:"%FIBER_TEST_LIBDIR%" %LIBS%
if errorlevel 1 goto fail
link /nologo /out:"%OUT%\fiber-workload-libco.exe" "%OUT%\main.obj" "%OUT%\bridge.obj" /LIBPATH:"%FIBER_TEST_LIBDIR%" %LIBS%
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
