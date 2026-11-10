@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined ALU_PROGRAM_OUT set ALU_PROGRAM_OUT=build\tests\alu-program-msvc
set OUT=%ALU_PROGRAM_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\SPIRV-Headers\include libretro\core_alu_spv.c tools\tests\core_alu_spv_build_test.c /Fo"%OUT%/" /Fd"%OUT%\unit.pdb" /Fe"%OUT%\build.exe"
if errorlevel 1 goto fail
if "%ALU_CONTROL_TEST%"=="1" (
 "%OUT%\build.exe" "%OUT%\arithmetic.spv" "%OUT%\program.spv" "%OUT%\control.spv"
) else (
 "%OUT%\build.exe" "%OUT%\arithmetic.spv" "%OUT%\program.spv"
)
if errorlevel 1 goto fail
if not "%GPU_TESTS%"=="1" goto success
cl /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\SPIRV-Headers\include /c libretro\core_alu_spv.c /Fo"%OUT%\emitter.obj"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /I. /c libretro\core_spv_binary.c /Fo"%OUT%\binary.obj"
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\Vulkan-Headers\include /c tools\tests\core_alu_spv_vulkan_test.c /Fo"%OUT%\arithmetic.obj"
if errorlevel 1 goto fail
cl /nologo /MD "%OUT%\emitter.obj" "%OUT%\binary.obj" "%OUT%\arithmetic.obj" /Fe"%OUT%\arithmetic.exe"
if errorlevel 1 goto fail
"%OUT%\arithmetic.exe"
if errorlevel 1 goto fail
if defined ALU_PROGRAM_BUILD set FORWARD_FLOW_BUILD=%ALU_PROGRAM_BUILD%
set FORWARD_FLOW_OUT=%OUT%\native
if "%ALU_CONTROL_TEST%"=="1" set ALU_PROGRAM_FLAGS=%ALU_PROGRAM_FLAGS% /DXE_ALU_CONTROL_TEST
if not defined ALU_PROGRAM_FIXTURE set ALU_PROGRAM_FIXTURE=tools\tests\core_alu_program_vulkan_test.c
if not defined ALU_PROGRAM_BRIDGE set ALU_PROGRAM_BRIDGE=tools\tests\alu_program_bridge.cc
set FORWARD_FLOW_SOURCE=%ALU_PROGRAM_FIXTURE%
set FORWARD_FLOW_BRIDGE=%ALU_PROGRAM_BRIDGE%
set FORWARD_FLOW_FLAGS=%FORWARD_FLOW_FLAGS% %ALU_PROGRAM_FLAGS% /Ithird_party\Vulkan-Headers\include
set FORWARD_FLOW_OBJECT="%OUT%\emitter.obj" "%OUT%\binary.obj"
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
