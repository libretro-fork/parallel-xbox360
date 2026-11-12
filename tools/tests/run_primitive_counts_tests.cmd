@echo off
setlocal
if defined PRIMITIVE_COUNTS_BUILD set FORWARD_FLOW_BUILD=%PRIMITIVE_COUNTS_BUILD%
if defined PRIMITIVE_COUNTS_OUT set FORWARD_FLOW_OUT=%PRIMITIVE_COUNTS_OUT%
if not defined FORWARD_FLOW_OUT set FORWARD_FLOW_OUT=build\tests\primitive-counts-msvc
set FORWARD_FLOW_FLAGS=%FORWARD_FLOW_FLAGS% /DXE_PRIMITIVE_NATIVE
set FORWARD_FLOW_SOURCE=tools\tests\core_primitive_builtin_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\primitive_counts_bridge.cc
call "%~dp0run_forward_flow_tests.cmd"
exit /b %errorlevel%
