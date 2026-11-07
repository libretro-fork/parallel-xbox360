@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined BARRIER_TEST_BUILD_DIR set BARRIER_TEST_BUILD_DIR=build\tests\d3d12-barriers-msvc
set OUT=%BARRIER_TEST_BUILD_DIR%
if not exist %OUT% mkdir %OUT%
set INC=/I. /Isrc /Ilibretro /Ithird_party/libretro-common/include /Ithird_party/fmt/include /Ithird_party/Vulkan-Headers/include /Ithird_party/DirectX-Headers/include/directx /Ithird_party /Ithird_party/ffmpeg-xenia /Ithird_party/FFmpeg /Ibuild/make/windows-x86_64/generated
set FLAGS=%BARRIER_TEST_FLAGS% /FItools/tests/core_command_rows_profile.h /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DFMT_HEADER_ONLY /DXENIA_LIBRETRO /DVULKAN_HPP_NO_TO_STRING /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /DASIO_STANDALONE /DASIO_NO_DEPRECATED /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS
cl %BARRIER_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX %INC% /c tools/tests/core_d3d12_barrier_test.c /Fd%OUT%\test.pdb /Fo%OUT%\test.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% %INC% /c tools/tests/core_d3d12_barrier_bridge.cc /Fd%OUT%\bridge.pdb /Fo%OUT%\bridge.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% %INC% /c src/xenia/gpu/d3d12/deferred_command_list.cc /Fd%OUT%\commands.pdb /Fo%OUT%\commands.obj
if not "%errorlevel%"=="0" goto fail
cl %BARRIER_TEST_FLAGS% /nologo /MD %OUT%\*.obj /Fe%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
