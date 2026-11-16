@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined UCODE_REUSE_OUT set UCODE_REUSE_OUT=build\tests\ucode-reuse-msvc
if not defined UCODE_REUSE_BUILD set UCODE_REUSE_BUILD=build\windows-native
set OUT=%UCODE_REUSE_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=%UCODE_REUSE_FLAGS% /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DRETRO_ATOMIC_FORCE_MSVC /I. /Ithird_party\libretro-common\include
cl %FLAGS% tools/tests/core_shader_ucode_reuse_test.c /Fo"%OUT%\unit.obj" /Fd"%OUT%\unit.pdb" /Fe"%OUT%\unit.exe"
if errorlevel 1 goto fail
"%OUT%\unit.exe"
if errorlevel 1 goto fail
cl %FLAGS% /c tools/tests/core_shader_ucode_reuse_threads_test.c /Fo"%OUT%\threads.obj" /Fd"%OUT%\threads.pdb"
if errorlevel 1 goto fail
cl %FLAGS% /c libretro/core_shader_ucode_reuse.c /Fo"%OUT%\reuse.obj" /Fd"%OUT%\reuse.pdb"
if errorlevel 1 goto fail
cl %UCODE_REUSE_FLAGS% /nologo /MD "%OUT%\threads.obj" "%OUT%\reuse.obj" "%UCODE_REUSE_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\threads.exe" /link /CGTHREADS:3
if errorlevel 1 goto fail
"%OUT%\threads.exe"
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
