@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined DRAW_CONTRACT_BUILD set DRAW_CONTRACT_BUILD=build\windows-native
if not defined DRAW_TRANSLATE_OUT set DRAW_TRANSLATE_OUT=build\tests\draw-translate-msvc
set OUT=%DRAW_TRANSLATE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=%DRAW_CONTRACT_FLAGS% /nologo /MD /O2 /DNDEBUG /DXENIA_LIBRETRO /DXE_GPU_DRAW_STATS=1 /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_CRT_SECURE_NO_WARNINGS
set INC=/I. /Isrc /Ithird_party/libretro-common/include /Ithird_party/fmt/include
cl %FLAGS% /TC /W4 /WX %INC% /c libretro/core_draw_stats.c /Fo"%OUT%\stats.obj" /Fd"%OUT%\stats.pdb"
if errorlevel 1 goto fail
set FORWARD_FLOW_OUT=%OUT%
set FORWARD_FLOW_BUILD=%DRAW_CONTRACT_BUILD%
set FORWARD_FLOW_SOURCE=tools\tests\core_draw_translate_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\float_rtz_bridge.cc
set FORWARD_FLOW_FLAGS=/DXE_GPU_DRAW_STATS=1 %DRAW_CONTRACT_FLAGS%
set FORWARD_FLOW_OBJECT=%OUT%\stats.obj %OUT%\translator.obj
cl %FLAGS% /EHsc /std:c++20 /utf-8 /arch:AVX %INC% /c src/xenia/gpu/shader_translator.cc /Fo"%OUT%\translator.obj" /Fd"%OUT%\translator.pdb"
if errorlevel 1 goto fail
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
"%OUT%\test.exe" "%OUT%" --negative
if not "%errorlevel%"=="1" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
