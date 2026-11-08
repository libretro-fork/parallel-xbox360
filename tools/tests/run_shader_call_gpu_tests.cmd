@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined SHADER_CALL_GPU_OUT set SHADER_CALL_GPU_OUT=build\tests\shader-call-gpu-msvc
set FORWARD_FLOW_OUT=%SHADER_CALL_GPU_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_shader_call_gpu_fixture.c
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl %SHADER_CALL_GPU_FLAGS% /nologo /TC /O2 /MD /W4 /WX /I. /external:Ithird_party\Vulkan-Headers\include /external:W0 tools\tests\core_shader_call_vulkan_test.c /Fo"%SHADER_CALL_GPU_OUT%\runtime.obj" /Fd"%SHADER_CALL_GPU_OUT%\runtime.pdb" /Fe"%SHADER_CALL_GPU_OUT%\runtime.exe"
if errorlevel 1 goto fail
"%SHADER_CALL_GPU_OUT%\runtime.exe" "%SHADER_CALL_GPU_OUT%"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
