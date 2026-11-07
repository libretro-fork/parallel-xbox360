@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined IMAGE_SINK_TEST_OUT set IMAGE_SINK_TEST_OUT=build\tests\spv-image-sink-msvc
set OUT=%IMAGE_SINK_TEST_OUT%
if not exist %OUT%\modules mkdir %OUT%\modules
for %%S in (tools\tests\core_spv_image_sink_test.c libretro\core_spv_image_sink.c libretro\core_spv_binary.c libretro\core_spv_size.c) do (
 cl %IMAGE_SINK_TEST_FLAGS% /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I. /Ilibretro /Ithird_party/libretro-common/include /FItools/tests/core_spv_image_sink_alloc.h /c %%S /Fd%OUT%\%%~nS.pdb /Fo%OUT%\%%~nS.obj
 if errorlevel 1 goto fail
)
cl %IMAGE_SINK_TEST_FLAGS% /nologo /MD %OUT%\core_spv_image_sink_test.obj %OUT%\core_spv_image_sink.obj %OUT%\core_spv_binary.obj %OUT%\core_spv_size.obj /Fe%OUT%\test.exe
if errorlevel 1 goto fail
%OUT%\test.exe %OUT%/modules
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
