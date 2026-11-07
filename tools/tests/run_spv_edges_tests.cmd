@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
set OUT=build\tests\spv-edges-msvc
set FLAGS=/O2
if "%SPV_EDGES_ASAN%"=="1" (
 set OUT=build\tests\spv-edges-msvc-asan
 set FLAGS=/O1 /Zi /fsanitize=address
)
if not exist %OUT% mkdir %OUT%
cl /nologo /TC /MD %FLAGS% /Fd%OUT%\symbols.pdb /W4 /WX /Ilibretro /Ithird_party/libretro-common/include tools/tests/core_spv_edges_test.c /Fo%OUT%\leaf.obj /Fe%OUT%\leaf.exe
if errorlevel 1 goto fail
%OUT%\leaf.exe
if errorlevel 1 goto fail
if "%SPV_EDGES_ASAN%"=="1" (
 for %%M in (relocate-uaf clear-uaf) do (
  %OUT%\leaf.exe %%M > %OUT%\%%M.log 2>&1
  if not errorlevel 1 goto fail
  findstr /c:"use-after-poison" %OUT%\%%M.log >nul
  if errorlevel 1 goto fail
 )
 echo PASS: MSVC ASan catches relocated and cleared edge views
)
popd
exit /b 0
:fail
popd
exit /b 1
