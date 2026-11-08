@echo off
setlocal
if not defined XMA_BENCH_INPUT exit /b 2
if not defined XMA_BENCH_CHANNELS set XMA_BENCH_CHANNELS=2
if not defined XMA_BENCH_BUILD set XMA_BENCH_BUILD=build\windows-native
if not defined XMA_BENCH_OUT set XMA_BENCH_OUT=build\tests\xma-native-benchmark
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not exist "%XMA_BENCH_OUT%" mkdir "%XMA_BENCH_OUT%"
cl /nologo /TC /MD /O2 %XMA_BENCH_FLAGS% /D_CRT_SECURE_NO_WARNINGS /Ithird_party\libretro-common\include /Ithird_party\FFmpeg /Ithird_party\ffmpeg-xenia tools\tests\xma_native_benchmark.c third_party\libretro-common\formats\wma\rwma_pro.c third_party\libretro-common\formats\wma\rwma_standard.c /Fo"%XMA_BENCH_OUT%\\" /Fe"%XMA_BENCH_OUT%\test.exe" /link /LIBPATH:"%XMA_BENCH_BUILD%\obj\Windows" libavcodec.lib libavutil.lib bcrypt.lib ole32.lib user32.lib advapi32.lib
if errorlevel 1 goto fail
"%XMA_BENCH_OUT%\test.exe" "%XMA_BENCH_INPUT%" %XMA_BENCH_CHANNELS%
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
