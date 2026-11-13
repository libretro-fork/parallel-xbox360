@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined ANALYSIS_TEXT_BUILD set ANALYSIS_TEXT_BUILD=build\windows-native
if not defined ANALYSIS_TEXT_OUT set ANALYSIS_TEXT_OUT=build\tests\analysis-text-msvc
if not exist "%ANALYSIS_TEXT_OUT%" mkdir "%ANALYSIS_TEXT_OUT%"
cl %ANALYSIS_TEXT_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /FItools\tests\shader_analysis_allocation_hook.h /c src\xenia\base\string_buffer.cc /Fo"%ANALYSIS_TEXT_OUT%\string-buffer.obj"
if errorlevel 1 goto fail
set FORWARD_FLOW_BUILD=%ANALYSIS_TEXT_BUILD%
set FORWARD_FLOW_OUT=%ANALYSIS_TEXT_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_shader_analysis_text_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\shader_analysis_text_bridge.cc
set FORWARD_FLOW_OBJECT="%ANALYSIS_TEXT_OUT%\string-buffer.obj" %ANALYSIS_TEXT_OBJECT%
set FORWARD_FLOW_FLAGS=%FORWARD_FLOW_FLAGS% /DXE_ANALYSIS_TEXT_ALLOCATIONS
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1