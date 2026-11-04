@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set TEST_SOURCE=%ROOT%\tools\tests\kernel_reference_test.cc
set TEST_BUILD_DIR=%ROOT%\build\tests\kernel-reference-msvc
call "%~dp0run_shader_publication_tests.cmd" %1