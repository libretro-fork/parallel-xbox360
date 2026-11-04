@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt, after the CMake core build.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set BUILD=%~1
if "%BUILD%"=="" set BUILD=%ROOT%\build\cmake-msvc-native
set OUT=%ROOT%\build\tests\shader-publication-msvc
if not exist "%OUT%" mkdir "%OUT%"
type nul > "%OUT%\libs.rsp"
for %%L in ("%BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
cl /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I"%ROOT%" /I"%ROOT%\src" /I"%ROOT%\third_party\libretro-common\include" /I"%ROOT%\third_party\fmt\include" "%ROOT%\tools\tests\shader_publication_test.cc" /Fo"%OUT%\test.obj" /c
if errorlevel 1 exit /b 1
link /nologo /OUT:"%OUT%\test.exe" "%OUT%\test.obj" @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib
if errorlevel 1 exit /b 1
"%OUT%\test.exe"