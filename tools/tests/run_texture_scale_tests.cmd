@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined TEXTURE_SCALE_OUT set TEXTURE_SCALE_OUT=build\tests\texture-scale-msvc
if not exist "%TEXTURE_SCALE_OUT%" mkdir "%TEXTURE_SCALE_OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_texture_lanes_test.c /Fo"%TEXTURE_SCALE_OUT%\plan.obj" /Fe"%TEXTURE_SCALE_OUT%\plan.exe"
if errorlevel 1 goto fail
"%TEXTURE_SCALE_OUT%\plan.exe"
if errorlevel 1 goto fail
set FORWARD_FLOW_OUT=%TEXTURE_SCALE_OUT%
set FORWARD_FLOW_SOURCE=tools\tests\core_texture_scale_fixture.c
set FORWARD_FLOW_BRIDGE=tools\tests\texture_sampling_bridge.cc
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl /nologo /TC /O2 /MD /W4 /WX /I. /external:Ithird_party\Vulkan-Headers\include /external:W0 tools\tests\core_texture_scale_vulkan_test.c /Fo"%TEXTURE_SCALE_OUT%\runtime.obj" /Fd"%TEXTURE_SCALE_OUT%\runtime.pdb" /Fe"%TEXTURE_SCALE_OUT%\runtime.exe"
if errorlevel 1 goto fail
"%TEXTURE_SCALE_OUT%\runtime.exe" "%TEXTURE_SCALE_OUT%"
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
