@echo off
setlocal
if defined VERTEX_ADDRESS_BUILD set FORWARD_FLOW_BUILD=%VERTEX_ADDRESS_BUILD%
if defined VERTEX_ADDRESS_OUT set FORWARD_FLOW_OUT=%VERTEX_ADDRESS_OUT%
if not defined FORWARD_FLOW_OUT set FORWARD_FLOW_OUT=build\tests\vertex-address-msvc
set FORWARD_FLOW_SOURCE=tools\tests\core_vertex_address_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\vertex_address_bridge.cc
call "%~dp0run_forward_flow_tests.cmd"
exit /b %errorlevel%
