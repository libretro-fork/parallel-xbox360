@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined DXIL_FORWARD_BUILD set DXIL_FORWARD_BUILD=build\windows-native
if not defined DXIL_FORWARD_OUT set DXIL_FORWARD_OUT=build\tests\dxil-forward-flow-msvc
set OUT=%DXIL_FORWARD_OUT%
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /DXE_DXIL_FLOW_TEST /Isrc\xenia\gpu\dxil /Isrc\xenia\gpu\dxil\bytecode tools\tests\dxil_writer_test.c tools\tests\dxil_flow_test.c tools\tests\dxil_alu_test.c /Fo"%OUT%/" /Fe"%OUT%\generator.exe" /link "%DXIL_FORWARD_BUILD%\obj\Windows\xenia-native-dxil.lib"
if errorlevel 1 goto fail
for /L %%I in (0,1,4) do (
 "%OUT%\generator.exe" "%OUT%\case%%I.dxil" flow %%I
 if errorlevel 1 goto fail
)
for /L %%I in (11,1,43) do (
 "%OUT%\generator.exe" "%OUT%\case%%I.dxil" flow %%I
 if errorlevel 1 goto fail
)
for /L %%I in (5,1,10) do (
 "%OUT%\generator.exe" "%OUT%\case%%I.dxil" flow %%I
 if not errorlevel 1 goto fail
)
echo PASS: MSVC C89 writer, 38 flow fixtures, state-slot counts and six invalid programs
popd
exit /b 0
:fail
popd
exit /b 1
