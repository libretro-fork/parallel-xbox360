@echo off
setlocal
if defined ALU_OPERAND_BUILD set FORWARD_FLOW_BUILD=%ALU_OPERAND_BUILD%
if not defined ALU_OPERAND_OUT set ALU_OPERAND_OUT=build\tests\alu-operand-native-msvc
set FORWARD_FLOW_OUT=%ALU_OPERAND_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_alu_operand_native_test.c
set FORWARD_FLOW_BRIDGE=tools\tests\alu_operand_bridge.cc
call "%~dp0run_forward_flow_tests.cmd"
exit /b %errorlevel%
