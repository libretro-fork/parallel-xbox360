@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined AUDIO_STATE_BUILD set AUDIO_STATE_BUILD=build\windows-native
if not defined AUDIO_DIRECT_OUT set AUDIO_DIRECT_OUT=build\tests\audio-direct-msvc
if not exist "%AUDIO_DIRECT_OUT%" mkdir "%AUDIO_DIRECT_OUT%"
cl %AUDIO_STATE_FLAGS% /nologo /TC /O2 /MD /W4 /WX /Ilibretro libretro\core_audio_convert.c tools\tests\core_audio_convert_test.c /Fo"%AUDIO_DIRECT_OUT%\\" /Fd"%AUDIO_DIRECT_OUT%\convert.pdb" /Fe"%AUDIO_DIRECT_OUT%\convert.exe"
if errorlevel 1 goto fail
"%AUDIO_DIRECT_OUT%\convert.exe"
if errorlevel 1 goto fail
for %%P in (0 1) do (
  if not exist "%AUDIO_DIRECT_OUT%\%%P" mkdir "%AUDIO_DIRECT_OUT%\%%P"
  cl %AUDIO_STATE_FLAGS% /nologo /TC /O2 /MD /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /DXE_AUDIO_QUEUE_PLAIN=%%P /Ilibretro /Ithird_party\libretro-common\include libretro\core_audio_queue.c libretro\core_audio_convert.c tools\tests\core_audio_queue_test.c tools\tests\core_audio_queue_state_test.c /Fo"%AUDIO_DIRECT_OUT%\%%P\\" /Fd"%AUDIO_DIRECT_OUT%\%%P\test.pdb" /Fe"%AUDIO_DIRECT_OUT%\%%P\test.exe" /link "%AUDIO_STATE_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib
  if errorlevel 1 goto fail
  "%AUDIO_DIRECT_OUT%\%%P\test.exe"
  if errorlevel 1 goto fail
  cl %AUDIO_STATE_FLAGS% /nologo /TC /O2 /MD /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /DXE_AUDIO_REGISTRY_TEST /DXE_AUDIO_QUEUE_PLAIN=%%P /Ilibretro /Ithird_party\libretro-common\include libretro\core_audio_queue.c libretro\core_audio_convert.c tools\tests\core_audio_registry_test.c /Fo"%AUDIO_DIRECT_OUT%\%%P\\" /Fd"%AUDIO_DIRECT_OUT%\%%P\registry.pdb" /Fe"%AUDIO_DIRECT_OUT%\%%P\registry.exe" /link "%AUDIO_STATE_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib
  if errorlevel 1 goto fail
  "%AUDIO_DIRECT_OUT%\%%P\registry.exe"
  if errorlevel 1 goto fail
)
popd
exit /b 0
:fail
popd
exit /b 1
