@echo off
setlocal
if not defined BINDING_TRANSLATE_BUILD set BINDING_TRANSLATE_BUILD=build\windows-native
set FORWARD_FLOW_BUILD=%BINDING_TRANSLATE_BUILD%
set FORWARD_FLOW_OUT=build\tests\binding-translate-msvc
set FORWARD_FLOW_SOURCE=tools\tests\core_binding_translate_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\binding_translate_bridge.cc
call tools\tests\run_forward_flow_tests.cmd
exit /b %errorlevel%
