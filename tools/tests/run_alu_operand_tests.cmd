@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined ALU_OPERAND_OUT set ALU_OPERAND_OUT=build\tests\alu-operand-msvc
if not exist "%ALU_OPERAND_OUT%" mkdir "%ALU_OPERAND_OUT%"
cl %ALU_OPERAND_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools\tests\core_alu_operand_test.c /Fo"%ALU_OPERAND_OUT%\test.obj" /Fd"%ALU_OPERAND_OUT%\test.pdb" /Fe"%ALU_OPERAND_OUT%\test.exe"
if errorlevel 1 goto fail
"%ALU_OPERAND_OUT%\test.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
