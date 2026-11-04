@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
set TEST_SOURCE=%ROOT%\tools\tests\render_target_collection_test.cc
set TEST_BUILD_DIR=%ROOT%\build\tests\render-target-collection-msvc
call "%~dp0run_shader_publication_tests.cmd" %*
exit /b %errorlevel%