@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt after building the core.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set CL=/I"%ROOT%\third_party\ffmpeg-xenia" /I"%ROOT%\third_party\FFmpeg" %CL%
set TEST_SOURCE=%ROOT%\tools\tests\audio_state_test.cc
set TEST_BUILD_DIR=%ROOT%\build\tests\audio-state-msvc
if not exist "%TEST_BUILD_DIR%" mkdir "%TEST_BUILD_DIR%"
cl /nologo /TC /MD /O2 /W4 /WX /I"%ROOT%" /I"%ROOT%\third_party\libretro-common\include" "%ROOT%\tools\tests\core_xma_packet_test.c" /Fo"%TEST_BUILD_DIR%\layout.obj" /Fe"%TEST_BUILD_DIR%\layout.exe"
if errorlevel 1 exit /b 1
"%TEST_BUILD_DIR%\layout.exe"
if errorlevel 1 exit /b 1
call "%~dp0run_shader_publication_tests.cmd"
exit /b %errorlevel%
