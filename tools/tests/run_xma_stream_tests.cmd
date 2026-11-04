@echo off
setlocal
if not defined XMA_INPUT_RAW (
  echo Set XMA_INPUT_RAW to the data chunk of FFmpeg samples/game-formats/xma/Title_Call.xma.
  exit /b 2
)
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set TEST_SOURCE=%ROOT%\tools\tests\xma_stream_test.cc
set TEST_BUILD_DIR=%ROOT%\build\tests\xma-stream-msvc
call "%~dp0run_shader_publication_tests.cmd" %1
exit /b %errorlevel%
