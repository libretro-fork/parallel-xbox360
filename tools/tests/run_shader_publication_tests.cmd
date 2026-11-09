@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt, after the CMake core build.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set BUILD=%~1
if "%BUILD%"=="" set BUILD=%ROOT%\build\cmake-msvc-native
set OUT=%ROOT%\build\tests\shader-publication-msvc
if defined TEST_BUILD_DIR set OUT=%TEST_BUILD_DIR%
set SOURCE=%TEST_SOURCE%
if not defined SOURCE set SOURCE=%ROOT%\tools\tests\shader_publication_test.cc
if not exist "%OUT%" mkdir "%OUT%"
type nul > "%OUT%\libs.rsp"
for %%L in ("%BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
cl /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I"%ROOT%" /I"%ROOT%\src" /I"%ROOT%\third_party\libretro-common\include" /I"%ROOT%\third_party\fmt\include" "%SOURCE%" /Fo"%OUT%\test.obj" /c
if errorlevel 1 exit /b 1
link /nologo /OUT:"%OUT%\test.exe" "%OUT%\test.obj" "%BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_events.c.obj" "%BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_disc.c.obj" @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib xinput.lib comctl32.lib shcore.lib dxguid.lib d3d12.lib dxgi.lib synchronization.lib version.lib uuid.lib gdi32.lib imm32.lib setupapi.lib cfgmgr32.lib hid.lib dwmapi.lib comdlg32.lib oleaut32.lib
if errorlevel 1 exit /b 1
"%OUT%\test.exe"