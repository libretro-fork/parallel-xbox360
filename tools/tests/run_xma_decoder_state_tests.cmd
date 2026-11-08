@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined DECODER_STATE_BUILD set DECODER_STATE_BUILD=build\windows-native
if not defined DECODER_STATE_OUT set DECODER_STATE_OUT=build\tests\xma-decoder-state-msvc
set OUT=%DECODER_STATE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
if "%DECODER_STATE_OPEN_FAILURE_TEST%"=="1" set DECODER_STATE_FLAGS=%DECODER_STATE_FLAGS% /DXE_XMA_OPEN_FAIL_TEST
cl %DECODER_STATE_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /c src\xenia\apu\xma_context_new.cc /Fo"%OUT%\context.obj"
if errorlevel 1 goto fail
cl %DECODER_STATE_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /c src\xenia\apu\xma_decoder.cc /Fo"%OUT%\decoder.obj"
if errorlevel 1 goto fail
cl %DECODER_STATE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include /c libretro\core_xma_native.c /Fo"%OUT%\native.obj"
if errorlevel 1 goto fail
cl %DECODER_STATE_FLAGS% /nologo /TC /MD /O2 /D_CRT_SECURE_NO_WARNINGS /Dcalloc=xe_xma_test_calloc /Dfree=xe_xma_test_free /Ithird_party\libretro-common\include /c third_party\libretro-common\formats\wma\rwma_pro.c /Fo"%OUT%\pro.obj"
if errorlevel 1 goto fail
cl %DECODER_STATE_FLAGS% /nologo /TC /MD /O2 /D_CRT_SECURE_NO_WARNINGS /Dcalloc=xe_xma_test_calloc /Dfree=xe_xma_test_free /Ithird_party\libretro-common\include /c third_party\libretro-common\formats\wma\rwma_standard.c /Fo"%OUT%\standard.obj"
if errorlevel 1 goto fail
set DECODER_STATE_OBJECT="%OUT%\context.obj" "%OUT%\decoder.obj" "%OUT%\native.obj" "%OUT%\pro.obj" "%OUT%\standard.obj"
cl %DECODER_STATE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DXE_DECODER_CLOCK_TEST /D_CRT_SECURE_NO_WARNINGS /I. /Ithird_party\libretro-common\include /c tools\tests\core_xma_decoder_state_test.c /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if not "%errorlevel%"=="0" goto fail
cl %DECODER_STATE_FLAGS% /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I. /Isrc /Ithird_party\libretro-common\include /Ithird_party\fmt\include /c tools\tests\xma_decoder_state_bridge.cc /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if not "%errorlevel%"=="0" goto fail
cl %DECODER_STATE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /Dcpu_features_get_time_usec=xe_decoder_gate_time /I. /Ithird_party\libretro-common\include /c libretro\core_pause_gate.c /Fo"%OUT%\gate.obj" /Fd"%OUT%\gate.pdb"
if not "%errorlevel%"=="0" goto fail
type nul > "%OUT%\libs.rsp"
for %%L in ("%DECODER_STATE_BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
cl %DECODER_STATE_FLAGS% /nologo /MD "%OUT%\driver.obj" "%OUT%\bridge.obj" "%OUT%\gate.obj" %DECODER_STATE_OBJECT% "%DECODER_STATE_BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_events.c.obj" "%DECODER_STATE_BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_disc.c.obj" @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib xinput.lib comctl32.lib shcore.lib dxguid.lib d3d12.lib dxgi.lib synchronization.lib version.lib uuid.lib gdi32.lib imm32.lib setupapi.lib cfgmgr32.lib hid.lib dwmapi.lib comdlg32.lib oleaut32.lib /Fe"%OUT%\test.exe" /link /OPT:REF
if not "%errorlevel%"=="0" goto fail
"%OUT%\test.exe"
if not "%errorlevel%"=="0" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
