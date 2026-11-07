@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined TEST_BUILD_DIR set TEST_BUILD_DIR=build\tests\disc-msvc
set OUT=%TEST_BUILD_DIR%
if not exist %OUT% mkdir %OUT%
set FLAGS=%DISC_TEST_FLAGS% /nologo /TC /MD /O2 /DXE_TEST_DISC_PATH_REFERENCE /D_GNU_SOURCE /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /Fd%OUT%\disc.pdb /I. /Ilibretro /Ithird_party/libretro-common/include
cl %FLAGS% /c third_party/libretro-common/streams/file_stream.c /Fo%OUT%\file_stream.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/vfs/vfs_implementation.c /Fo%OUT%\vfs_implementation.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/file/file_path.c /Fo%OUT%\file_path.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/file/file_path_io.c /Fo%OUT%\file_path_io.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/string/stdstring.c /Fo%OUT%\stdstring.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/string/rstrtod.c /Fo%OUT%\rstrtod.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/encodings/encoding_utf.c /Fo%OUT%\encoding_utf.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/compat/compat_strcasestr.c /Fo%OUT%\compat_strcasestr.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/compat/compat_posix_string.c /Fo%OUT%\compat_posix_string.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/compat/fopen_utf8.c /Fo%OUT%\fopen_utf8.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/compat/compat_strl.c /Fo%OUT%\compat_strl.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/time/rtime.c /Fo%OUT%\rtime.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/rthreads/rthreads.c /Fo%OUT%\rthreads.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/rthreads/retro_eventcount.c /Fo%OUT%\retro_eventcount.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/features/features_cpu.c /Fo%OUT%\features_cpu.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /c third_party/libretro-common/hash/lrc_hash.c /Fo%OUT%\hash.obj
if not "%errorlevel%"=="0" goto fail
cl %FLAGS% /W4 /WX /c tools/tests/core_disc_test.c /Fo%OUT%\test.obj
if not "%errorlevel%"=="0" goto fail
cl %DISC_TEST_FLAGS% /nologo /MD /O2 /std:c++20 /EHsc /Fd%OUT%\reference.pdb /c tools/tests/core_disc_path_reference.cc /Fo%OUT%\path_reference.obj
if not "%errorlevel%"=="0" goto fail
cl %DISC_TEST_FLAGS% /nologo /MD %OUT%\*.obj /Fe%OUT%\test.exe /link winmm.lib
if not "%errorlevel%"=="0" goto fail
%OUT%\test.exe
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1