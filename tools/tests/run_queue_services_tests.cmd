@echo off
setlocal
if not defined QUEUE_SERVICES_OUT set QUEUE_SERVICES_OUT=build\tests\queue-services-msvc
set FORWARD_FLOW_OUT=%QUEUE_SERVICES_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_queue_services_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\queue_services_bridge.cc
call "%~dp0run_forward_flow_tests.cmd"
exit /b %errorlevel%
