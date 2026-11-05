@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\spv-output-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_words_test.c /Fo%OUT%\words-unit.obj /Fe%OUT%\words-unit.exe
if errorlevel 1 goto fail
%OUT%\words-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX tools/tests/core_spv_binary_test.c /Fo%OUT%\unit.obj /Fe%OUT%\unit.exe
if errorlevel 1 goto fail
%OUT%\unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_size_test.c /Fo%OUT%\size-unit.obj /Fe%OUT%\size-unit.exe
if errorlevel 1 goto fail
%OUT%\size-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_walk_test.c /Fo%OUT%\walk-unit.obj /Fe%OUT%\walk-unit.exe
if errorlevel 1 goto fail
%OUT%\walk-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_plan_test.c /Fo%OUT%\plan-unit.obj /Fe%OUT%\plan-unit.exe
if errorlevel 1 goto fail
%OUT%\plan-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_index_test.c /Fo%OUT%\index-unit.obj /Fe%OUT%\index-unit.exe
if errorlevel 1 goto fail
%OUT%\index-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_id_range_test.c /Fo%OUT%\range-unit.obj /Fe%OUT%\range-unit.exe
if errorlevel 1 goto fail
%OUT%\range-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_block_chain_test.c /Fo%OUT%\chain-unit.obj /Fe%OUT%\chain-unit.exe
if errorlevel 1 goto fail
%OUT%\chain-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_block_state_test.c /Fo%OUT%\block-state-unit.obj /Fe%OUT%\block-state-unit.exe
if errorlevel 1 goto fail
%OUT%\block-state-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_instruction_chain_test.c /Fo%OUT%\instruction-chain-unit.obj /Fe%OUT%\instruction-chain-unit.exe
if errorlevel 1 goto fail
%OUT%\instruction-chain-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /c libretro/core_spv_size.c /Fo%OUT%\size.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_object_pool_test.c /Fo%OUT%\object-unit.obj /Fe%OUT%\object-unit.exe
if errorlevel 1 goto fail
%OUT%\object-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_arena_test.c /Fo%OUT%\arena-unit.obj /Fe%OUT%\arena-unit.exe
if errorlevel 1 goto fail
%OUT%\arena-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /c libretro/core_spv_arena.c /Fo%OUT%\arena.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /c libretro/core_spv_binary.c /Fo%OUT%\binary.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /c libretro/core_spv_operands.c /Fo%OUT%\operands.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /I. /FItools/tests/core_spv_arena_test_alloc.h /c libretro/core_spv_binary.c /Fo%OUT%\binary-fault.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /c tools/tests/core_spv_arena_test_alloc.c /Fo%OUT%\fault.obj
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /DXE_SPV_BINARY_CHECK /FItools/tests/core_spv_arena_test_alloc.h /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_module_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\bridge.exe /link %OUT%\fault.obj %OUT%\size.obj %OUT%\binary-fault.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
for %%V in (0 1 2 3 4 5 6 7) do (
 %OUT%\bridge.exe %OUT%\debug-%%V.spv %%V
 if errorlevel 1 goto fail
)
echo PASS: native MSVC builder vector/C89 output, source chunks and failure recovery
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc /Ithird_party/libretro-common/include tools/tests/core_spv_views_bridge.cc src/xenia/gpu/spirv_builder.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\views.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
for %%V in (0 1 2 3 4 5 6 7) do (
 %OUT%\views.exe %OUT%\views-%%V.spv %%V
 if errorlevel 1 goto fail
)
echo PASS: native MSVC borrowed inputs and unrelated access-chain state
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /I. /FItools/tests/core_spv_arena_test_alloc.h /c libretro/core_spv_operands.c /Fo%OUT%\operands-fault.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /I. /FItools/tests/core_spv_arena_test_alloc.h /c libretro/core_spv_arena.c /Fo%OUT%\arena-fault.obj
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /FItools/tests/core_spv_arena_test_alloc.h /I. /Isrc /Ithird_party/libretro-common/include tools/tests/core_spv_arena_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\arena-bridge.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\arena-fault.obj %OUT%\operands-fault.obj %OUT%\fault.obj
if errorlevel 1 goto fail
%OUT%\arena-bridge.exe
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_sampler_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\sampler-bridge.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\arena.obj %OUT%\operands.obj
if errorlevel 1 goto fail
%OUT%\sampler-bridge.exe %OUT%\sampler.spv
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
