@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined RESOLVE_EMIT_OUT set RESOLVE_EMIT_OUT=build\tests\resolve-loads-emit-msvc
set OUT=%RESOLVE_EMIT_OUT%
if not exist %OUT% mkdir %OUT%
if not exist %OUT%\spv mkdir %OUT%\spv
if not exist %OUT%\dxil mkdir %OUT%\dxil
if not exist %OUT%\dxil-objects mkdir %OUT%\dxil-objects
powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/prepare_resolve_loads_emit.ps1 -OutputDirectory %OUT%
if errorlevel 1 goto fail
set INC=/I. /Isrc /Ilibretro /Itools/tests /Ithird_party/libretro-common/include /Ithird_party/fmt/include /Ithird_party /Ithird_party/Vulkan-Headers/include
set FLAGS=%RESOLVE_EMIT_FLAGS% /nologo /utf-8 /MD /O2 /EHsc /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DVULKAN_HPP_NO_TO_STRING /DFMT_HEADER_ONLY /D_CRT_SECURE_NO_WARNINGS
for %%U in (size binary packet arena) do (
 cl %RESOLVE_EMIT_FLAGS% /nologo /TC /MD /O2 /W4 /WX %INC% /c libretro/core_spv_%%U.c /Fd%OUT%\%%U.pdb /Fo%OUT%\%%U.obj
 if errorlevel 1 goto fail
)
for %%S in (%OUT%/static_emitters.cc tools/tests/core_resolve_loads_spv_bridge.cc src/xenia/gpu/spirv_builder.cc src/xenia/gpu/spirv/SpvBuilder.cpp src/xenia/gpu/spirv/InReadableOrder.cpp src/xenia/gpu/spirv/Logger.cpp src/xenia/gpu/edram_dump_shader.cc) do (
 cl %FLAGS% %INC% /c %%S /Fd%OUT%\%%~nS.pdb /Fo%OUT%\%%~nS.obj
 if errorlevel 1 goto fail
)
cl %RESOLVE_EMIT_FLAGS% /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS %INC% /c tools/tests/core_resolve_loads_emit_test.c /Fd%OUT%\test.pdb /Fo%OUT%\test.obj
if errorlevel 1 goto fail
cl %RESOLVE_EMIT_FLAGS% /nologo /MD %OUT%\test.obj %OUT%\static_emitters.obj %OUT%\core_resolve_loads_spv_bridge.obj %OUT%\spirv_builder.obj %OUT%\SpvBuilder.obj %OUT%\InReadableOrder.obj %OUT%\Logger.obj %OUT%\edram_dump_shader.obj %OUT%\size.obj %OUT%\binary.obj %OUT%\packet.obj %OUT%\arena.obj /Fe%OUT%\spv-test.exe
if errorlevel 1 goto fail
%OUT%\spv-test.exe %OUT%/spv
if errorlevel 1 goto fail
set DXINC=/I. /Itools/tests /Ithird_party/libretro-common/include /Isrc/xenia/gpu/dxil /Isrc/xenia/gpu/dxil/bytecode
for %%S in (src\xenia\gpu\dxil\*.c src\xenia\gpu\dxil\bytecode\*.c) do (
 if not "%%~nxS"=="edram_dump.c" (
  cl %RESOLVE_EMIT_FLAGS% /nologo /TC /MD /O2 /DNDEBUG %DXINC% /c %%S /Fd%OUT%\dxil-objects\%%~nS.pdb /Fo%OUT%\dxil-objects\%%~nS.obj
  if errorlevel 1 goto fail
 )
)
cl %RESOLVE_EMIT_FLAGS% /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DXE_TEST_RESOLVE_DXIL %DXINC% /c tools/tests/core_resolve_loads_emit_test.c /Fd%OUT%\dxil-test.pdb /Fo%OUT%\dxil-test.obj
if errorlevel 1 goto fail
cl %RESOLVE_EMIT_FLAGS% /nologo /TC /MD /O2 %DXINC% /FItools/tests/core_resolve_loads_dxil_hook.h /c src/xenia/gpu/dxil/edram_dump.c /Fd%OUT%\dxil-dump.pdb /Fo%OUT%\dxil-dump.obj
if errorlevel 1 goto fail
cl %RESOLVE_EMIT_FLAGS% /nologo /MD %OUT%\dxil-test.obj %OUT%\dxil-dump.obj %OUT%\dxil-objects\*.obj /Fe%OUT%\dxil-test.exe
if errorlevel 1 goto fail
%OUT%\dxil-test.exe %OUT%/dxil
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
