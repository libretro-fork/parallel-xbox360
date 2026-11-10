@echo off
setlocal
if not defined ANALYSIS_TEXT_BUILD set ANALYSIS_TEXT_BUILD=build\windows-native
set FORWARD_FLOW_BUILD=%ANALYSIS_TEXT_BUILD%
set FORWARD_FLOW_OUT=build\tests\analysis-text-msvc
set FORWARD_FLOW_SOURCE=tools\tests\core_shader_analysis_text_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\shader_analysis_text_bridge.cc
call tools\tests\run_forward_flow_tests.cmd
exit /b %errorlevel%
