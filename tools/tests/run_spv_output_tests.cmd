@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\spv-output-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX tools/tests/core_spv_binary_test.c /Fo%OUT%\unit.obj /Fe%OUT%\unit.exe
if errorlevel 1 goto fail
%OUT%\unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /c libretro/core_spv_binary.c /Fo%OUT%\binary.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /c libretro/core_spv_operands.c /Fo%OUT%\operands.obj
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /DXE_SPV_BINARY_CHECK /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_module_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\bridge.exe /link %OUT%\binary.obj %OUT%\operands.obj
if errorlevel 1 goto fail
for %%V in (0 1 2 3 4) do (
 %OUT%\bridge.exe %OUT%\debug-%%V.spv %%V
 if errorlevel 1 goto fail
)
echo PASS: native MSVC builder vector/C89 output, source chunks and failure recovery
popd
exit /b 0
:fail
popd
exit /b 1
