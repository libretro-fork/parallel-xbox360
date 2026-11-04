@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set OUT=%ROOT%\build\tests\state-msvc
if not exist "%OUT%" mkdir "%OUT%"
set COMMON=%ROOT%\third_party\libretro-common
cl /nologo /TC /MD /O2 /W4 /WX /I"%ROOT%" "%ROOT%\tools\tests\core_log_line_test.c" /Fo"%OUT%\log-line.obj" /Fe"%OUT%\log-line.exe"
if errorlevel 1 exit /b 1
"%OUT%\log-line.exe"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /I"%ROOT%" /I"%COMMON%\include" "%ROOT%\tools\tests\core_state_io_test.c" /Fo"%OUT%\cursor.obj" /Fe"%OUT%\cursor.exe"
if errorlevel 1 exit /b 1
"%OUT%\cursor.exe"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I"%ROOT%" /I"%COMMON%\include" /c "%ROOT%\libretro\core_pause_gate.c" /Fo"%OUT%\gate.obj"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC /I"%ROOT%" /I"%COMMON%\include" "%ROOT%\tools\tests\core_pause_snapshot_test.c" /Fo"%OUT%\pause.obj" /Fe"%OUT%\pause.exe" /link "%OUT%\gate.obj" "%ROOT%\build\cmake-msvc-native\obj\Windows\retro-common.lib" winmm.lib
if errorlevel 1 exit /b 1
"%OUT%\pause.exe"
if errorlevel 1 exit /b 1
cl /nologo /TP /MD /O2 /std:c++20 /EHsc /I"%ROOT%" /I"%ROOT%\src" /I"%COMMON%\include" /Fo"%OUT%\\" /Fe"%OUT%\stream.exe" "%ROOT%\tools\tests\byte_stream_state_test.cc" "%ROOT%\src\xenia\base\byte_stream.cc"
if errorlevel 1 exit /b 1
"%OUT%\stream.exe"
exit /b %errorlevel%
