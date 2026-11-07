@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined TRACE_TEST_OUT set TRACE_TEST_OUT=build\tests\gpu-trace-msvc
set OUT=%TRACE_TEST_OUT%
if not exist %OUT% mkdir %OUT%
if not exist %OUT%\host mkdir %OUT%\host
cl /nologo /TC /O2 /D_CRT_SECURE_NO_WARNINGS tools/build/core_codegen.c tools/build/core_deflate.c /Fo%OUT%\host\ /Fe%OUT%\host\codegen.exe
if errorlevel 1 goto fail
%OUT%\host\codegen.exe inputs %OUT% windows msvc
if errorlevel 1 goto fail
set COMMON=third_party\libretro-common
set FLAGS=%TRACE_TEST_FLAGS% /nologo /MD /O2 /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /D_WIN32_WINNT=0x0A00
set INC=/I%OUT% /I. /Isrc /Ilibretro /I%COMMON%/include /Ithird_party/fmt/include /Ithird_party/snappy
> %OUT%\version.h echo #define XE_BUILD_COMMIT "0123456789012345678901234567890123456789"
for %%S in (streams/file_stream.c vfs/vfs_implementation.c file/file_path.c file/file_path_io.c string/stdstring.c string/rstrtod.c encodings/encoding_utf.c compat/compat_strcasestr.c compat/compat_posix_string.c compat/fopen_utf8.c compat/compat_strl.c time/rtime.c rthreads/rthreads.c rthreads/retro_eventcount.c features/features_cpu.c encodings/encoding_xxh3.c encodings/encoding_xxh64.c) do (
 cl %FLAGS% /TC %INC% /c %COMMON%/%%S /Fd%OUT%\%%~nS.pdb /Fo%OUT%\%%~nS.obj
 if errorlevel 1 goto fail
)
for %%S in (libretro/core_file.c libretro/core_cache_file.c tools/tests/core_gpu_trace_test.c) do (
 cl %FLAGS% /TC /W4 /WX %INC% /c %%S /Fd%OUT%\%%~nS.pdb /Fo%OUT%\%%~nS.obj
 if errorlevel 1 goto fail
)
for %%S in (third_party/snappy/snappy.cc third_party/snappy/snappy-sinksource.cc third_party/snappy/snappy-stubs-internal.cc src/xenia/gpu/trace_writer.cc tools/tests/core_gpu_trace_bridge.cc) do (
 cl %FLAGS% /EHsc /std:c++20 /arch:AVX /utf-8 /DNDEBUG /DXENIA_LIBRETRO /DXE_OPTION_GPU_TRACE %INC% /c %%S /Fd%OUT%\%%~nS.pdb /Fo%OUT%\%%~nS.obj
 if errorlevel 1 goto fail
)
cl %TRACE_TEST_FLAGS% /nologo /MD %OUT%\*.obj winmm.lib /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe %OUT%\current.xtr
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
