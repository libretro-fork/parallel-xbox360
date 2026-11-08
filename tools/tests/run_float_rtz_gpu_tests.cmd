@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined FLOAT_RTZ_GPU_OUT set FLOAT_RTZ_GPU_OUT=build\tests\float-rtz-gpu-msvc
set FORWARD_FLOW_OUT=%FLOAT_RTZ_GPU_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_float_rtz_gpu_fixture.c
set FORWARD_FLOW_BRIDGE=tools\tests\float_rtz_bridge.cc
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl /nologo /TC /MD /O2 /W4 /WX /DXE_VERTEX_TEST_CASES=576 /DXE_FLOAT_RTZ_GPU_TEST /Itools\tests /external:Ithird_party\Vulkan-Headers\include /external:W0 tools\tests\core_shader_call_vulkan_test.c /Fo"%FLOAT_RTZ_GPU_OUT%\runtime.obj" /Fe"%FLOAT_RTZ_GPU_OUT%\runtime.exe"
if errorlevel 1 goto fail
"%FLOAT_RTZ_GPU_OUT%\runtime.exe" "%FLOAT_RTZ_GPU_OUT%"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
