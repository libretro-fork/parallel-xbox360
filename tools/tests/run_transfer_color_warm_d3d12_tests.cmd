@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
cd /d "%ROOT%"
if not defined COLOR_D3D12_WARM_OUT set COLOR_D3D12_WARM_OUT=build\tests\transfer-color-warm-d3d12-msvc
if not defined UINT_IMAGE_BUILD set UINT_IMAGE_BUILD=build\windows-native
set OUT=%COLOR_D3D12_WARM_OUT%
if not exist "%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" exit /b 2
if exist "%OUT%/null.csv" exit /b 2
if not exist "%OUT%" mkdir "%OUT%"
set FLAGS=/nologo /TC /MD /O2 /W4 /WX /wd4201 /D_CRT_SECURE_NO_WARNINGS /DXE_COLOR_D3D12_IMAGE_TEST /DXE_COLOR_D3D12_WARM_TEST /I. /Ithird_party/libretro-common/include /Isrc/xenia/gpu/dxil/bytecode
set SOURCES=tools/tests/transfer_color_d3d12_init_test.c libretro/core_transfer_color_d3d12.c libretro/core_transfer_uint_d3d12.c src/xenia/gpu/dxil/transfer_uint.c
set LIBS="%UINT_IMAGE_BUILD%/obj/Windows/xenia-native-dxil.lib" d3d12.lib dxgi.lib dxguid.lib /CGTHREADS:3 /DEBUG
cl %FLAGS% %SOURCES% /Fo"%OUT%/" /Fe"%OUT%/control.exe" /link %LIBS%
if errorlevel 1 exit /b 1
set XE_COLOR_WARM_CONTROL=1
for %%M in (null pair reverse) do (
 set XE_COLOR_WARM_SAME_PIPELINE=
 set XE_COLOR_WARM_REVERSE=
 if "%%M"=="null" set XE_COLOR_WARM_SAME_PIPELINE=1
 if "%%M"=="reverse" set XE_COLOR_WARM_REVERSE=1
 set XE_COLOR_D3D12_WARM_CSV=%OUT%/%%M.csv
 "%OUT%/control.exe" > "%OUT%/%%M.log" 2>&1
 if errorlevel 1 exit /b 1
)
python tools/tests/transfer_color_warm_d3d12_analysis.py "%OUT%/null.csv" --out "%OUT%/null.json" --software --controls-only --self-check
if errorlevel 1 exit /b 1
python tools/tests/transfer_color_warm_d3d12_analysis.py "%OUT%/pair.csv" --out "%OUT%/pair.json" --software --controls-only
if errorlevel 1 exit /b 1
python tools/tests/transfer_color_warm_d3d12_analysis.py "%OUT%/reverse.csv" --out "%OUT%/reverse.json" --software --controls-only --reverse
if errorlevel 1 exit /b 1
cl %FLAGS% /DXE_COLOR_WARM_MISSING_WORK %SOURCES% /Fo"%OUT%/" /Fe"%OUT%/missing-work.exe" /link %LIBS%
if errorlevel 1 exit /b 1
set XE_COLOR_WARM_SAME_PIPELINE=
set XE_COLOR_WARM_REVERSE=
set XE_COLOR_D3D12_WARM_CSV=%OUT%/missing-work.csv
"%OUT%/missing-work.exe" > "%OUT%/missing-work.log" 2>&1
if not "%ERRORLEVEL%"=="1" exit /b 1
set MISSING_WORK_LOG=%OUT:/=\%\missing-work.log
findstr /c:"color warm missing GPU work" "%MISSING_WORK_LOG%" >nul
if errorlevel 1 exit /b 1
set XE_COLOR_WARM_CONTROL=0
set XE_COLOR_D3D12_WARM_CSV=%OUT%/invalid-mode.csv
"%OUT%/control.exe" > "%OUT%/invalid-mode.log" 2>&1
if not "%ERRORLEVEL%"=="2" exit /b 1
set INVALID_MODE_LOG=%OUT:/=\%\invalid-mode.log
findstr /c:"invalid color warm flag XE_COLOR_WARM_CONTROL" "%INVALID_MODE_LOG%" >nul
if errorlevel 1 exit /b 1
echo PASS: C89 color warm controls, paired order, GPU work counters, missing-draw and foreign-mode rejection; not performance acceptance
exit /b 0
