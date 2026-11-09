@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
pushd "%ROOT%"
if not defined DRAW_CONTRACT_BUILD set DRAW_CONTRACT_BUILD=build\windows-native
if not defined DRAW_CONTRACT_OUT set DRAW_CONTRACT_OUT=build\tests\draw-contract-msvc
set OUT=%DRAW_CONTRACT_OUT%
if not exist "%OUT%" mkdir "%OUT%"
set INC=/I. /Isrc /Itools\tests /Ithird_party\libretro-common\include /Ithird_party\fmt\include
set FLAGS=%DRAW_CONTRACT_FLAGS% /nologo /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DRETRO_ATOMIC_FORCE_MSVC
cl %FLAGS% /TC %INC% tools\tests\core_draw_stats_test.c /Fo"%OUT%\release.obj" /Fe"%OUT%\release.exe"
if errorlevel 1 goto fail
"%OUT%\release.exe"
if errorlevel 1 goto fail
for %%S in (libretro/core_draw_stats.c libretro/core_job_pool.c tools/tests/core_draw_stats_test.c) do (
 cl %FLAGS% /TC /DXE_GPU_DRAW_STATS=1 %INC% /c %%S /Fo"%OUT%\%%~nS.obj" /Fd"%OUT%\%%~nS.pdb"
 if errorlevel 1 goto fail
)
cl %FLAGS% /EHsc /std:c++20 /DXE_GPU_DRAW_STATS=1 %INC% /c tools\tests\draw_contract_bridge.cc /Fo"%OUT%\bridge.obj" /Fd"%OUT%\bridge.pdb"
if errorlevel 1 goto fail
cl %FLAGS% "%OUT%\core_draw_stats.obj" "%OUT%\core_job_pool.obj" "%OUT%\core_draw_stats_test.obj" "%OUT%\bridge.obj" "%DRAW_CONTRACT_BUILD%\obj\Windows\retro-common.lib" winmm.lib ws2_32.lib bcrypt.lib /Fe"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe"
if errorlevel 1 goto fail
"%OUT%\test.exe" --negative
if not "%errorlevel%"=="1" goto fail
popd
exit /b 0
:fail
popd
exit /b 1
