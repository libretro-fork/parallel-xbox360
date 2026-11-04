@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt after building the core.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set CL=/I"%ROOT%\third_party\ffmpeg-xenia" /I"%ROOT%\third_party\FFmpeg" %CL%
set TEST_SOURCE=%ROOT%\tools\tests\audio_state_test.cc
set TEST_BUILD_DIR=%ROOT%\build\tests\audio-state-msvc
call "%~dp0run_shader_publication_tests.cmd"
exit /b %errorlevel%
