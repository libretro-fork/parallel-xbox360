@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined VARIANT_INDEX_OUT set VARIANT_INDEX_OUT=build\tests\shader-variant-index
if not exist "%VARIANT_INDEX_OUT%" mkdir "%VARIANT_INDEX_OUT%"
if not defined VARIANT_INDEX_UNIT set VARIANT_INDEX_UNIT=tools\tests\core_shader_variant_index_test.c
if not defined VARIANT_INDEX_SOURCE set VARIANT_INDEX_SOURCE=tools\tests\core_shader_variant_owner_test.c
if not defined VARIANT_INDEX_BRIDGE set VARIANT_INDEX_BRIDGE=tools\tests\shader_variant_index_bridge.cc
cl /nologo /TC /MD /O2 /W4 /WX /I. "%VARIANT_INDEX_UNIT%" /Fo"%VARIANT_INDEX_OUT%/" /Fe"%VARIANT_INDEX_OUT%\unit.exe"
if errorlevel 1 goto fail
"%VARIANT_INDEX_OUT%\unit.exe"
if errorlevel 1 goto fail
if defined VARIANT_INDEX_BUILD set FORWARD_FLOW_BUILD=%VARIANT_INDEX_BUILD%
set FORWARD_FLOW_OUT=%VARIANT_INDEX_OUT%\native
set FORWARD_FLOW_SOURCE=%VARIANT_INDEX_SOURCE%
set FORWARD_FLOW_BRIDGE=%VARIANT_INDEX_BRIDGE%
set FORWARD_FLOW_OBJECT=
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
