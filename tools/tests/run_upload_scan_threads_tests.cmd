@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\upload-scan-threads-msvc
if not exist %OUT% mkdir %OUT%
set FLAGS=/nologo /TC /MD /O2 /D_GNU_SOURCE /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I. /Ilibretro /Ithird_party/libretro-common/include
cl %FLAGS% /c third_party/libretro-common/streams/file_stream.c /Fo%OUT%\file_stream.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/vfs/vfs_implementation.c /Fo%OUT%\vfs_implementation.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/file/file_path.c /Fo%OUT%\file_path.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/file/file_path_io.c /Fo%OUT%\file_path_io.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/string/stdstring.c /Fo%OUT%\stdstring.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/string/rstrtod.c /Fo%OUT%\rstrtod.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/encodings/encoding_utf.c /Fo%OUT%\encoding_utf.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/compat/compat_strcasestr.c /Fo%OUT%\compat_strcasestr.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/compat/compat_posix_string.c /Fo%OUT%\compat_posix_string.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/compat/fopen_utf8.c /Fo%OUT%\fopen_utf8.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/compat/compat_strl.c /Fo%OUT%\compat_strl.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/time/rtime.c /Fo%OUT%\rtime.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/rthreads/rthreads.c /Fo%OUT%\rthreads.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/rthreads/retro_eventcount.c /Fo%OUT%\retro_eventcount.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/features/features_cpu.c /Fo%OUT%\features_cpu.obj
if errorlevel 1 goto fail
cl %FLAGS% /c third_party/libretro-common/hash/lrc_hash.c /Fo%OUT%\hash.obj
if errorlevel 1 goto fail
cl %FLAGS% /W4 /WX /c tools/tests/core_upload_scan_threads_test.c /Fo%OUT%\test.obj
if errorlevel 1 goto fail
cl /nologo /MD %OUT%\*.obj /Fe%OUT%\test.exe /link winmm.lib
if errorlevel 1 goto fail
%OUT%\test.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1