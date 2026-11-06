@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\spv-output-msvc
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_packet_test.c /Fo%OUT%\packet-unit.obj /Fe%OUT%\packet-unit.exe
if errorlevel 1 goto fail
%OUT%\packet-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_extensions_test.c /Fo%OUT%\extensions-unit.obj /Fe%OUT%\extensions-unit.exe
if errorlevel 1 goto fail
%OUT%\extensions-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /c libretro/core_spv_probe.c /Fo%OUT%\probe.obj
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_metadata_test.c /Fo%OUT%\metadata-unit.obj /Fe%OUT%\metadata-unit.exe /link %OUT%\probe.obj
if errorlevel 1 goto fail
%OUT%\metadata-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_stack_test.c /Fo%OUT%\stack-unit.obj /Fe%OUT%\stack-unit.exe
if errorlevel 1 goto fail
%OUT%\stack-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_groups_test.c /Fo%OUT%\groups-unit.obj /Fe%OUT%\groups-unit.exe
if errorlevel 1 goto fail
%OUT%\groups-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_constant_cache_test.c /Fo%OUT%\constant-unit.obj /Fe%OUT%\constant-unit.exe
if errorlevel 1 goto fail
%OUT%\constant-unit.exe
if errorlevel 1 goto fail
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include tools/tests/core_spv_payload_test.c /Fo%OUT%\payload-unit.obj /Fe%OUT%\payload-unit.exe
if errorlevel 1 goto fail
%OUT%\payload-unit.exe
if errorlevel 1 goto fail
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
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /c libretro/core_spv_packet.c /Fo%OUT%\operands.obj
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
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_metadata_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\metadata.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj %OUT%\probe.obj
if errorlevel 1 goto fail
for %%V in (0 1 2 3) do (
 %OUT%\metadata.exe %OUT%\metadata-%%V.bin %%V
 if errorlevel 1 goto fail
)
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_stack_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\stack.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
%OUT%\stack.exe %OUT%\stack.bin
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_linkage_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\linkage.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
%OUT%\linkage.exe %OUT%\linkage.bin
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_constant_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\constant.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
%OUT%\constant.exe %OUT%\constant.bin 1
if errorlevel 1 goto fail
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_types_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\types.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
for %%V in (0 1) do (
 %OUT%\types.exe %OUT%\types-%%V.bin %%V
 if errorlevel 1 goto fail
)
cl /nologo /TC /MD /O2 /W4 /WX /Ithird_party/libretro-common/include /I. /FItools/tests/core_spv_arena_test_alloc.h /c libretro/core_spv_packet.c /Fo%OUT%\operands-fault.obj
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
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /DFMT_HEADER_ONLY /I. /Isrc /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include /Ithird_party/fmt/include tools/tests/core_spv_repeat_bridge.cc src/xenia/gpu/spirv_builder.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\repeat.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
for %%V in (0 1 2 3) do (
 %OUT%\repeat.exe %OUT%\repeat-%%V.spv %%V
 if errorlevel 1 goto fail
)
cl /nologo /MD /O2 /EHsc /std:c++20 /DNDEBUG /I. /Isrc/xenia/gpu/spirv /Ithird_party/libretro-common/include tools/tests/core_spv_extensions_bridge.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/SpvPostProcess.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp /Fo%OUT%\ /Fe%OUT%\extensions.exe /link %OUT%\size.obj %OUT%\binary.obj %OUT%\operands.obj %OUT%\arena.obj
if errorlevel 1 goto fail
for %%V in (0 1) do (
 %OUT%\extensions.exe %OUT%\extensions-%%V.bin %%V
 if errorlevel 1 goto fail
)
popd
exit /b 0
:fail
popd
exit /b 1
