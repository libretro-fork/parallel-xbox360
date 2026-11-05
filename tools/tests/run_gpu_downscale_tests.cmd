@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set OUT=%ROOT%\build\tests\gpu-downscale-msvc
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /O2 /MD /W4 /WX /I"%ROOT%" "%ROOT%\tools\tests\core_gpu_downscale_test.c" /Fo"%OUT%\plan.obj" /Fe"%OUT%\plan.exe"
if errorlevel 1 exit /b 1
"%OUT%\plan.exe"
if errorlevel 1 exit /b 1
cl /nologo /TC /O2 /MD /W4 /WX /I"%ROOT%" /external:I"%ROOT%\third_party\Vulkan-Headers\include" /external:W0 "%ROOT%\tools\tests\core_downscale_vulkan_test.c" /Fo"%OUT%\vulkan.obj" /Fe"%OUT%\vulkan.exe"
if errorlevel 1 exit /b 1
"%OUT%\vulkan.exe" "%ROOT%\tools\tests\data\resolve_downscale_reference.spv" "%ROOT%\src\xenia\gpu\shaders\bytecode\vulkan_spirv\resolve_downscale_cs.spv"
if errorlevel 1 exit /b 1
cl /nologo /TC /O2 /MD /W4 /WX /I"%ROOT%" "%ROOT%\tools\tests\core_downscale_d3d12_test.c" /Fo"%OUT%\d3d12.obj" /Fe"%OUT%\d3d12.exe" /link d3d12.lib dxgi.lib dxguid.lib
if errorlevel 1 exit /b 1
"%OUT%\d3d12.exe" "%ROOT%\src\xenia\gpu\shaders\bytecode\d3d12_dxil\resolve_downscale_cs.dxil"
exit /b %errorlevel%
