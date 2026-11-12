@echo off
setlocal
for %%R in ("%~dp0..\..") do set HARNESS=%%~fR
for %%R in ("%~1") do set SOURCE=%%~fR
for %%R in ("%~2") do set BUILD=%%~fR
for %%R in ("%~3") do set OUT=%%~fR
if "%~3"=="" exit /b 2
if not exist "%BUILD%\obj\Windows\xenia-gpu.lib" exit /b 2
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I"%SOURCE%" /I"%SOURCE%\third_party\libretro-common\include" /c "%HARNESS%\tools\tests\core_primitive_ranges_bench.c" /Fo"%OUT%\driver.obj" /Fd"%OUT%\driver.pdb"
if errorlevel 1 exit /b 1
cl /nologo /MD /O2 /EHsc /utf-8 /std:c++20 /arch:AVX /DNDEBUG /DXENIA_LIBRETRO /DRETRO_ATOMIC_FORCE_MSVC /I"%SOURCE%" /I"%SOURCE%\src" /I"%SOURCE%\third_party\libretro-common\include" /I"%SOURCE%\third_party\fmt\include" /c "%HARNESS%\tools\tests\primitive_counts_bridge.cc" /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if errorlevel 1 exit /b 1
type nul > "%OUT%\libs.rsp"
for %%L in ("%BUILD%\obj\Windows\*.lib") do echo "%%~fL">> "%OUT%\libs.rsp"
cl /nologo /MD "%OUT%\driver.obj" "%OUT%\bridge.obj" "%BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_events.c.obj" "%BUILD%\libretro\CMakeFiles\xenia-libretro.dir\core_disc.c.obj" @"%OUT%\libs.rsp" ntdll.lib ws2_32.lib winmm.lib bcrypt.lib shlwapi.lib dbghelp.lib ole32.lib user32.lib shell32.lib advapi32.lib xinput.lib comctl32.lib shcore.lib dxguid.lib d3d12.lib dxgi.lib synchronization.lib version.lib uuid.lib gdi32.lib imm32.lib setupapi.lib cfgmgr32.lib hid.lib dwmapi.lib comdlg32.lib oleaut32.lib /Fe"%OUT%\test.exe" /link /OPT:REF /CGTHREADS:3
exit /b %errorlevel%
