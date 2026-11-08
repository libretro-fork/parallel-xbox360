@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined TEXTURE_SAMPLING_OUT set TEXTURE_SAMPLING_OUT=build\tests\texture-sampling-msvc
set FORWARD_FLOW_OUT=%TEXTURE_SAMPLING_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_texture_sampling_fixture.c
set FORWARD_FLOW_BRIDGE=tools\tests\texture_sampling_bridge.cc
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl %TEXTURE_SAMPLING_FLAGS% /nologo /TC /O2 /MD /W4 /WX /I. /external:Ithird_party\Vulkan-Headers\include /external:W0 tools\tests\core_texture_sampling_vulkan_test.c /Fo"%TEXTURE_SAMPLING_OUT%\runtime.obj" /Fd"%TEXTURE_SAMPLING_OUT%\runtime.pdb" /Fe"%TEXTURE_SAMPLING_OUT%\runtime.exe"
if errorlevel 1 goto fail
"%TEXTURE_SAMPLING_OUT%\runtime.exe" "%TEXTURE_SAMPLING_OUT%"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
