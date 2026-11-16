@echo off
setlocal
for %%R in ("%~dp0..\..") do set ROOT=%%~fR
if not defined PIPELINE_INDEX_PARENT_SOURCE exit /b 1
if not defined PIPELINE_INDEX_OUT set PIPELINE_INDEX_OUT=%ROOT%\build\tests\pipeline-index-native
for %%D in ("%PIPELINE_INDEX_OUT%") do set OUT=%%~fD
if defined PIPELINE_INDEX_TIMING if exist "%OUT%\samples.csv" exit /b 1
if not exist "%OUT%" mkdir "%OUT%"
set EXTRA=/fsanitize=address /Z7 /DXE_PIPELINE_INDEX_TRACK_NEW
if defined PIPELINE_INDEX_TIMING set EXTRA=
cl /nologo /TC /MD /O2 /W4 /WX %EXTRA% /I"%ROOT%" /I"%ROOT%/third_party/libretro-common/include" "%ROOT%/tools/tests/core_pipeline_hash_index_test.c" /Fo"%OUT%/index.obj" /Fe"%OUT%/index.exe" /link /DEBUG /CGTHREADS:1
if errorlevel 1 exit /b 1
"%OUT%/index.exe"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX %EXTRA% /I"%ROOT%/tools/tests" /c "%ROOT%/tools/tests/pipeline_hash_index_alloc.c" /Fo"%OUT%/alloc.obj"
if errorlevel 1 exit /b 1
call :module parent "%PIPELINE_INDEX_PARENT_SOURCE%"
if errorlevel 1 exit /b 1
call :module candidate "%ROOT%"
if errorlevel 1 exit /b 1
cl /nologo /TC /MD /O2 /W4 /WX /D_CRT_SECURE_NO_WARNINGS %EXTRA% "%ROOT%/tools/tests/pipeline_hash_index_fixture.c" /Fo"%OUT%/driver.obj" /Fe"%OUT%/driver.exe" /link /DEBUG /CGTHREADS:1
if errorlevel 1 exit /b 1
if defined PIPELINE_INDEX_TIMING goto timing
"%OUT%/driver.exe" "%OUT%/parent.dll" "%OUT%/candidate.dll"
if errorlevel 1 exit /b 1
call :module ignore-key "%ROOT%" /DXE_PIPELINE_INDEX_IGNORE_KEY
if errorlevel 1 exit /b 1
"%OUT%/driver.exe" "%OUT%/parent.dll" "%OUT%/ignore-key.dll" > "%OUT%\ignore-key.log" 2>&1
if not "%ERRORLEVEL%"=="1" exit /b 1
findstr /l /c:"queries[role][i]" "%OUT%\ignore-key.log" >nul
if errorlevel 1 exit /b 1
"%OUT%/driver.exe" "%OUT%/parent.dll" "%OUT%/parent.dll" > "%OUT%\same-module.log" 2>&1
if not "%ERRORLEVEL%"=="1" exit /b 1
findstr /l /c:"intrusive==0" "%OUT%\same-module.log" >nul
if errorlevel 1 exit /b 1
echo PASS: native pipeline owners, bookkeeping counts, balanced heap lifetime and two negative controls
exit /b 0
:timing
if not defined PYTHON set PYTHON=python
"%PYTHON%" -c "import os,sys;from pathlib import Path;sys.path.insert(0,os.path.join(os.environ['ROOT'],'tools/tests'));import shader_compile_ci as c;c.require_idle_runner(Path(os.environ['OUT'])/'runner-idle.jsonl','pipeline-index-before')"
if errorlevel 1 exit /b 1
"%OUT%/driver.exe" "%OUT%/parent.dll" "%OUT%/candidate.dll" "%OUT%/samples.csv"
if errorlevel 1 exit /b 1
"%PYTHON%" -c "import os,sys;from pathlib import Path;sys.path.insert(0,os.path.join(os.environ['ROOT'],'tools/tests'));import shader_compile_ci as c;c.require_idle_runner(Path(os.environ['OUT'])/'runner-idle.jsonl','pipeline-index-after')"
exit /b %ERRORLEVEL%
:module
set SOURCE=%~2
cl /nologo /MD /O2 /Ob3 /GS- /W1 /WX /EHsc /GR /utf-8 /std:c++20 /arch:AVX /wd4201 /DHAVE_THREADS /DHAVE_THREAD_STORAGE /DNDEBUG /DRETRO_ATOMIC_FORCE_MSVC /DXENIA_LIBRETRO /DVULKAN_HPP_NO_TO_STRING %EXTRA% %3 /I"%SOURCE%" /I"%SOURCE%/src" /I"%SOURCE%/third_party" /I"%SOURCE%/third_party/fmt/include" /I"%SOURCE%/third_party/libretro-common/include" /I"%ROOT%/tools/tests" /c "%ROOT%/tools/tests/pipeline_hash_index_bridge.cc" /Fo"%OUT%/%1.obj"
if errorlevel 1 exit /b 1
cl /nologo /MD /LD %EXTRA% "%OUT%/%1.obj" "%OUT%/alloc.obj" /Fe"%OUT%/%1.dll" /link /DEBUG /CGTHREADS:1
exit /b %ERRORLEVEL%
