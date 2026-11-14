@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined ALU_RESUME_OUT set ALU_RESUME_OUT=build\tests\alu-resume-msvc
if not defined ALU_RESUME_UNIT set ALU_RESUME_UNIT=tools\tests\core_alu_spv_build_test.c
if not defined ALU_RESUME_SOURCE set ALU_RESUME_SOURCE=tools\tests\core_alu_resume_vulkan_test.c
if not defined ALU_RESUME_BRIDGE set ALU_RESUME_BRIDGE=tools\tests\alu_program_bridge.cc
set OUT=%ALU_RESUME_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %ALU_RESUME_FLAGS% /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\SPIRV-Headers\include libretro\core_alu_spv.c "%ALU_RESUME_UNIT%" /Fo"%OUT%/" /Fd"%OUT%\unit.pdb" /Fe"%OUT%\build.exe"
if errorlevel 1 goto fail
"%OUT%\build.exe" "%OUT%\arithmetic.spv" "%OUT%\program.spv" "%OUT%\control.spv" "%OUT%\resume.spv"
if errorlevel 1 goto fail
if defined SPIRV_VALIDATOR (
 for %%V in (arithmetic program control resume) do (
  "%SPIRV_VALIDATOR%" --target-env vulkan1.0 "%OUT%\%%V.spv"
  if errorlevel 1 goto fail
 )
)
if not "%GPU_TESTS%"=="1" goto success
cl %ALU_RESUME_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\SPIRV-Headers\include /c libretro\core_alu_spv.c /Fo"%OUT%\emitter.obj"
if errorlevel 1 goto fail
cl %ALU_RESUME_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /c libretro\core_spv_binary.c /Fo"%OUT%\binary.obj"
if errorlevel 1 goto fail
if defined ALU_RESUME_BUILD set FORWARD_FLOW_BUILD=%ALU_RESUME_BUILD%
set FORWARD_FLOW_SOURCE=%ALU_RESUME_SOURCE%
set FORWARD_FLOW_BRIDGE=%ALU_RESUME_BRIDGE%
set FORWARD_FLOW_OBJECT="%OUT%\emitter.obj" "%OUT%\binary.obj"
set RESUME_COMMON_FLAGS=%FORWARD_FLOW_FLAGS% %ALU_RESUME_FLAGS% /Ithird_party\Vulkan-Headers\include /I"%ROOT%\tools\tests"
set FORWARD_FLOW_OUT=%OUT%\reference
set FORWARD_FLOW_FLAGS=%RESUME_COMMON_FLAGS% /DXE_RESUME_BASELINE
set XE_ALU_PROGRAM_DUMP=%FORWARD_FLOW_OUT%\observed.spv
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
fc /b "%OUT%\control.spv" "%FORWARD_FLOW_OUT%\observed.spv" >nul
if errorlevel 1 goto fail
set FORWARD_FLOW_OUT=%OUT%\continuation
set FORWARD_FLOW_FLAGS=%RESUME_COMMON_FLAGS%
set XE_ALU_PROGRAM_DUMP=%FORWARD_FLOW_OUT%\observed.spv
call tools\tests\run_forward_flow_tests.cmd
if errorlevel 1 goto fail
fc /b "%OUT%\resume.spv" "%FORWARD_FLOW_OUT%\observed.spv" >nul
if errorlevel 1 goto fail
:success
popd
exit /b 0
:fail
popd
exit /b 1
