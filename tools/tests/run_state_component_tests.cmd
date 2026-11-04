@echo off
setlocal
rem Run from a Visual Studio x64 developer prompt after building the core.
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
for %%T in (memory_state scheduler_state delay_state timer_state) do (
  call :run %%T
  if errorlevel 1 exit /b 1
)
exit /b 0
:run
set TEST_SOURCE=%ROOT%\tools\tests\%1_test.cc
set TEST_BUILD_DIR=%ROOT%\build\tests\%1-msvc
call "%~dp0run_shader_publication_tests.cmd"
exit /b %errorlevel%
