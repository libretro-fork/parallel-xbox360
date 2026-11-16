@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined SHADER_MEMEXPORT_OUT set SHADER_MEMEXPORT_OUT=build\tests\shader-memexport-msvc
set OUT=%SHADER_MEMEXPORT_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl %SHADER_MEMEXPORT_FLAGS% /nologo /TC /MD /O2 /W4 /WX /I. /Ithird_party\libretro-common\include tools/tests/core_shader_memexport_test.c /Fo"%OUT%/unit.obj" /Fd"%OUT%/unit.pdb" /Fe"%OUT%/unit.exe" /link /CGTHREADS:3
if errorlevel 1 exit /b 1
"%OUT%/unit.exe"
if errorlevel 1 exit /b 1
if defined SHADER_MEMEXPORT_BUILD (
 set FORWARD_FLOW_BUILD=%SHADER_MEMEXPORT_BUILD%
 set FORWARD_FLOW_OUT=%OUT%
 set FORWARD_FLOW_SOURCE=tools\tests\core_shader_memexport_native_test.c
 set FORWARD_FLOW_BRIDGE=tools\tests\shader_memexport_native_bridge.cc
 set FORWARD_FLOW_FLAGS=%SHADER_MEMEXPORT_FLAGS%
 set FORWARD_FLOW_LINK_FLAGS=/CGTHREADS:3
 call tools\tests\run_forward_flow_tests.cmd
 if errorlevel 1 exit /b 1
)
exit /b 0
