#!/usr/bin/env python3
"""CI orchestration only. Actual shader algorithms and timers are C89/typed ABI.

Same current harness against both revisions; no game files or installed cores.
Software results are deliberately not described as gameplay-stutter signoff.
"""
import argparse
import csv
import hashlib
import json
import math
import mmap
import os
from pathlib import Path
import platform
import re
import shutil
import statistics
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
WIN = os.name == "nt"
JOBS = max(1, min(5, (os.cpu_count() or 2) // 2))
SYSTEM_LIBS = "ntdll ws2_32 winmm bcrypt shlwapi dbghelp ole32 user32 shell32 advapi32 xinput comctl32 shcore dxguid d3d12 dxgi synchronization version uuid gdi32 imm32 setupapi cfgmgr32 hid dwmapi comdlg32 oleaut32".split()


def git(*args, cwd=ROOT):
    return subprocess.check_output(["git", *args], cwd=cwd, text=True).strip()


def select_commits(event, sha, before=None, pushed=None):
    """Every newly reachable commit, each compared with its own first parent."""
    sha = git("rev-parse", "--verify", sha + "^{commit}")
    if event == "push" and before and set(before) != {"0"}:
        git("merge-base", "--is-ancestor", before, sha)
        commits = git("rev-list", "--reverse", "--topo-order", before + ".." + sha).splitlines()
    elif event == "push" and pushed:
        ids={git("rev-parse","--verify",x+"^{commit}") for x in pushed}
        commits=[c for c in git("rev-list","--reverse","--topo-order",sha).splitlines() if c in ids]
        if set(commits)!=ids:raise ValueError("pushed commit not reachable from tip")
    else:
        commits = [sha]
    if not commits or len(commits) > 128:
        raise ValueError("empty push or too many commits for the 256-job hosted matrix; split explicitly")
    return [{"sha": c, "base": git("rev-parse", c + "^1")} for c in commits]


def run(command, source, log, env=None, timeout=3600):
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("a", encoding="utf-8") as output:
        output.write("COMMAND " + repr([str(x) for x in command]) + "\n")
        output.flush()
        subprocess.run([str(x) for x in command], cwd=source, env=env,
                       stdout=output, stderr=subprocess.STDOUT, check=True, timeout=timeout)


def cpu_build(source, build, out, logs, env):
    out.mkdir(parents=True, exist_ok=True)
    driver, bridge = HERE / "core_shader_compile_fixture.c", HERE / "shader_compile_bridge.cc"
    if WIN:
        inc = ["/I" + str(source), "/I" + str(source / "third_party/libretro-common/include")]
        run(["cl", "/nologo", "/TC", "/MD", "/O2", "/W4", "/WX", "/D_CRT_SECURE_NO_WARNINGS",
             *inc, "/c", driver, "/Fo" + str(out / "driver.obj")], source, logs, env)
        run(["cl", "/nologo", "/MD", "/O2", "/EHsc", "/utf-8", "/std:c++20", "/arch:AVX",
             "/DNDEBUG", "/DXENIA_LIBRETRO", "/DRETRO_ATOMIC_FORCE_MSVC", *inc,
             "/I" + str(source / "src"), "/I" + str(source / "third_party/fmt/include"),
             "/c", bridge, "/Fo" + str(out / "bridge.obj")], source, logs, env)
        libs = sorted((build / "obj/Windows").glob("*.lib"))
        if not libs:
            raise ValueError("native core archives missing")
        response = out / "libs.rsp"
        response.write_text("\n".join('"' + str(p) + '"' for p in libs), encoding="utf-8")
        extra = build / "libretro/CMakeFiles/xenia-libretro.dir"
        run(["cl", "/nologo", "/MD", out / "driver.obj", out / "bridge.obj",
             extra / "core_events.c.obj", extra / "core_disc.c.obj", "@" + str(response),
             *[x + ".lib" for x in SYSTEM_LIBS], "/Fe" + str(out / "cpu.exe"),
             "/link", "/OPT:REF", "/CGTHREADS:" + str(JOBS)], source, logs, env)
        return out / "cpu.exe"
    cc, cxx = env.get("CC", "cc"), env.get("CXX", "c++")
    inc = ["-I" + str(source), "-I" + str(source / "third_party/libretro-common/include")]
    run([cc, "-std=c89", "-pedantic", "-Wno-long-long", "-Wdeclaration-after-statement",
         "-Wall", "-Wextra", "-Werror", "-O2", *inc, "-c", driver, "-o", out / "driver.o"], source, logs, env)
    objects = source / ("build/make/linux-" + platform.machine() + "/objects.rsp")
    run([cxx, "-O2", "-std=c++20", "-Wno-attributes", "-DNDEBUG", "-DXENIA_LIBRETRO",
         "-DRETRO_ATOMIC_FORCE_GCC_NEW", *inc, "-I" + str(source / "src"),
         "-I" + str(source / "third_party/fmt/include"), out / "driver.o", bridge,
         "@" + str(objects), "-pthread", "-ldl", "-lm", "-lrt", "-Wl,--strip-debug", "-o", out / "cpu"], source, logs, env)
    return out / "cpu"


def validate_comparison_workload(log):
    observed=re.findall(r"^COMPARE_CONTEXT replays=(\d+) invocations=(\d+)$",log,re.MULTILINE)
    if observed!=[("1536","3072")]:raise ValueError("missing or substituted comparison context execution")


def validate_lazy_source_workload(log):
    observed=re.findall(r"^LAZY_SOURCE_CONTEXT replays=(\d+) invocations=(\d+)$",log,re.MULTILINE)
    if observed!=[("64","128")]:raise ValueError("missing or substituted lazy-source context execution")


def validate_raw_program(source, build, out, log, env, backend, control=False):
    """New component coverage; absent historical code is not a passing fixture."""
    script=source / "tools/tests" / ("run_alu_program_tests.cmd" if WIN else "run_alu_program_tests.sh")
    header=source / "libretro/core_alu_spv.h"
    available=script.is_file() and (not control or
        (header.is_file() and "xe_alu_control_spv_build(" in header.read_text()))
    coverage=dict(available=available,unit=False,vulkan_gpu=False,
                  renderer_integration=False,full_guest_shader_coverage=False)
    if not coverage["available"]:
        coverage["reason"]="historical revision has no raw "+("control-flow" if control else "program")+" kernel"
        return coverage
    fixture=env.copy()
    fixture.pop("XE_ALU_PROGRAM_TIMING_ONLY",None)
    fixture.pop("XE_ALU_PROGRAM_VALID_WORK",None)
    fixture.update(ALU_PROGRAM_OUT=str(out / ("raw-control" if control else "raw-program")),
                   ALU_PROGRAM_FIXTURE=str(HERE / "core_alu_program_vulkan_test.c"),
                   ALU_PROGRAM_BRIDGE=str(HERE / "alu_program_bridge.cc"),
                   GPU_TESTS="1" if backend.startswith("vulkan") else "0")
    fixture["ALU_CONTROL_TEST"]="1" if control else "0"
    if fixture["GPU_TESTS"]=="1":
        fixture["ALU_PROGRAM_FLAGS"]="/DXE_SHADER_COMPILE_BENCH" if WIN else "-DXE_SHADER_COMPILE_BENCH"
        fixture["XE_SHADER_BENCH_CSV"]=str(out / ("raw-control-prepare.csv" if control else "raw-program-prepare.csv"))
        fixture["XE_SHADER_BENCH_CPU_CSV"]=str(out / ("raw-control-prepare-cpu.csv" if control else "raw-program-prepare-cpu.csv"))
    if WIN:
        fixture["ALU_PROGRAM_BUILD"]=str(build)
        fixture["FORWARD_FLOW_LINK_FLAGS"]="/CGTHREADS:"+str(JOBS)
        command=[os.environ.get("COMSPEC","cmd.exe"),"/d","/c",script]
    else:
        command=["sh",script]
    run(command,source,log,fixture,240)
    coverage.update(unit=True,vulkan_gpu=fixture["GPU_TESTS"]=="1",corpus_version=1,timing_profile_version=2,
                    pinned_fixture_sha256=file_sha256(HERE/"core_alu_program_vulkan_test.c"),
                    pinned_bridge_sha256=file_sha256(HERE/"alu_program_bridge.cc"))
    if control:
        content=header.read_text()
        boolean="#define XE_ALU_CONTROL_BOOL 1" in content
        loops="#define XE_ALU_CONTROL_LOOPS 1" in content
        predicate="#define XE_ALU_CONTROL_PREDICATE 1" in content
        comparison="#define XE_ALU_CONTROL_COMPARE 1" in content
        lazy_source="#define XE_ALU_CONTROL_LAZY_SOURCE 1" in content
        if comparison and coverage["vulkan_gpu"]:validate_comparison_workload(log.read_text())
        if lazy_source and coverage["vulkan_gpu"]:validate_lazy_source_workload(log.read_text())
        coverage.update(control_flow_version=6 if lazy_source else 5 if comparison else 4 if predicate else 3 if loops else 2 if boolean else 1,
                        boolean_cf_supported=boolean,nonempty_loops_supported=loops,
                        predicate_cf_supported=predicate,
                        comparison_alu_supported=comparison,
                        lazy_source_supported=lazy_source,
                        timed_corpus_version=3,boolean_context_cases=224 if boolean and coverage["vulkan_gpu"] else 0,
                        loop_context_cases=36 if loops and coverage["vulkan_gpu"] else 0,
                        predicate_context_cases=1254 if predicate and coverage["vulkan_gpu"] else 0,
                        comparison_context_cases=1536 if comparison and coverage["vulkan_gpu"] else 0,
                        lazy_source_context_cases=64 if lazy_source and coverage["vulkan_gpu"] else 0,
                        context_cases_untimed=True,timed_child_context_replays=0,
                        cf_step_limit=4096,call_depth_limit=4,
                        pinned_control_fixture_sha256=file_sha256(HERE/"shader_control_program_fixture.h"))
    return coverage


def validate_loop_reference(source, build, out, log, env, backend):
    """Keep the packed-program oracle's loop semantics under literal goldens."""
    header=source / "libretro/core_shader_loop.h"
    coverage=dict(available=header.is_file() and "xe_shader_loop_advance(" in header.read_text(),
                  unit=False,native=False,vulkan_gpu=False)
    if not coverage["available"]:
        coverage["reason"]="historical interpreter predates corrected repeat-loop state"
        return coverage
    directory=out / "loop-correctness"
    directory.mkdir(parents=True,exist_ok=True)
    unit=directory / ("unit.exe" if WIN else "unit")
    if WIN:
        run(["cl","/nologo","/TC","/MD","/O2","/W4","/WX",
             "/I"+str(source),"/I"+str(source / "third_party/libretro-common/include"),
             HERE / "core_shader_loop_test.c","/Fo"+str(directory / "unit.obj"),"/Fe"+str(unit)],source,log,env)
    else:
        run([env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
             "-I"+str(source),"-I"+str(source / "third_party/libretro-common/include"),
             HERE / "core_shader_loop_test.c","-o",unit],source,log,env)
    run([unit],source,log,env,30)
    fixture=env.copy()
    fixture.update(FORWARD_FLOW_SOURCE=str(HERE / "core_shader_loop_gpu_fixture.c"),
                   FORWARD_FLOW_BRIDGE=str(HERE / "shader_loop_accuracy_bridge.cc"),
                   FORWARD_FLOW_OUT=str(directory))
    if WIN:
        fixture.update(FORWARD_FLOW_BUILD=str(build),FORWARD_FLOW_LINK_FLAGS="/CGTHREADS:"+str(JOBS))
        run([os.environ.get("COMSPEC","cmd.exe"),"/d","/c",source / "tools/tests/run_forward_flow_tests.cmd"],source,log,fixture,240)
    else:
        fixture["CXX"]=env.get("CXX","c++")+" -Wl,--strip-debug"
        fixture["SPIRV_VALIDATOR"]=shutil.which("spirv-val") or "spirv-val"
        run(["sh",source / "tools/tests/run_forward_flow_tests.sh"],source,log,fixture,240)
    coverage.update(unit=True,native=True,cases=7,
                    pinned_fixture_sha256=file_sha256(HERE / "core_shader_loop_gpu_fixture.c"),
                    pinned_bridge_sha256=file_sha256(HERE / "shader_loop_accuracy_bridge.cc"))
    if backend.startswith("vulkan"):
        runtime=directory / ("vulkan.exe" if WIN else "vulkan")
        if WIN:
            run(["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/D_CRT_SECURE_NO_WARNINGS",
                 "/DXE_LOOP_ACCURACY_GPU_TEST","/DXE_VERTEX_TEST_CASES=7",
                 "/I"+str(source / "third_party/Vulkan-Headers/include"),HERE / "core_shader_call_vulkan_test.c",
                 "/Fo"+str(directory / "vulkan.obj"),"/Fe"+str(runtime)],source,log,env)
        else:
            run([env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
                 "-DXE_LOOP_ACCURACY_GPU_TEST","-DXE_VERTEX_TEST_CASES=7","-isystem",source / "third_party/Vulkan-Headers/include",
                 HERE / "core_shader_call_vulkan_test.c","-ldl","-o",runtime],source,log,env)
        run([runtime,directory],source,log,env,120)
        coverage["vulkan_gpu"]=True
    return coverage


def validate_ucode_snapshot(source, build, out, log, env):
    directory=out/"ucode-snapshot";directory.mkdir(parents=True,exist_ok=True)
    coverage=dict(native=False,owner_unit=False,owner_available=(source/"libretro/core_shader_ucode.h").is_file(),cases=96)
    inc=[("/I" if WIN else "-I")+str(source),("/I" if WIN else "-I")+str(source/"third_party/libretro-common/include")]
    if coverage["owner_available"]:
        unit=directory/("unit.exe" if WIN else "unit")
        if WIN:
            command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX",*inc,HERE/"core_shader_ucode_test.c","/Fo"+str(directory/"unit.obj"),"/Fe"+str(unit)]
        else:
            command=[env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",*inc,HERE/"core_shader_ucode_test.c","-o",unit]
        run(command,source,log,env,60);run([unit],source,log,env,30);coverage["owner_unit"]=True
    fixture=env.copy();fixture.update(FORWARD_FLOW_SOURCE=str(HERE/"core_shader_ucode_native_test.c"),
        FORWARD_FLOW_BRIDGE=str(HERE/"shader_ucode_bridge.cc"),FORWARD_FLOW_OUT=str(directory),
        XE_SHADER_BENCH_CSV=str(directory/"prepare.csv"),XE_SHADER_BENCH_CPU_CSV=str(directory/"prepare-cpu.csv"))
    fixture["FORWARD_FLOW_FLAGS"]=("/DXE_SHADER_COMPILE_BENCH /I" if WIN else "-DXE_SHADER_COMPILE_BENCH -I")+str(HERE)
    if WIN:
        fixture.update(FORWARD_FLOW_BUILD=str(build),FORWARD_FLOW_LINK_FLAGS="/CGTHREADS:"+str(JOBS))
        run([os.environ.get("COMSPEC","cmd.exe"),"/d","/c",source/"tools/tests/run_forward_flow_tests.cmd"],source,log,fixture,240)
    else:
        fixture["CXX"]=env.get("CXX","c++")+" -Wl,--strip-debug"
        run(["sh",source/"tools/tests/run_forward_flow_tests.sh"],source,log,fixture,240)
    coverage["native"]=True
    (directory/"coverage.json").write_text(json.dumps(coverage,indent=2))
    return directory/("test.exe" if WIN else "test")


def prepare(source, out, results, backend, reuse_build=None):
    out.mkdir(parents=True,exist_ok=True)
    env = os.environ.copy()
    build = Path(reuse_build).resolve() if reuse_build else source / "build/shader-perf-core"
    log = results / (source.name + "-build.log")
    env.pop("XE_SHADER_BENCH_CPU_CSV",None)
    clock=out/("clock.exe" if WIN else "clock")
    if WIN:
        run(["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/D_CRT_SECURE_NO_WARNINGS",
             HERE/"core_shader_clock_test.c","/Fo"+str(out/"clock.obj"),"/Fe"+str(clock)],source,log,env)
    else:
        run([env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
             HERE/"core_shader_clock_test.c","-o",clock],source,log,env)
    validate_clock_fixture(clock,results/"clock-checks"/(out.parent.name+"-"+out.name))
    if not reuse_build:
        if WIN:
            run(["cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl"], source, log, env)
            run(["cmake", "--build", build, "--target", "xenia-libretro", "--parallel", JOBS], source, log, env)
        else:
            run(["make", "-j" + str(JOBS)], source, log, env)
    loop_coverage=validate_loop_reference(source,build,out,log,env,backend)
    (results/(source.name+"-"+out.parent.name+"-"+out.name+"-loops.json")).write_text(
        json.dumps(loop_coverage,indent=2),encoding="utf-8")
    program_coverage=validate_raw_program(source,build,out,log,env,backend)
    (results/(source.name+"-"+out.parent.name+"-"+out.name+"-raw-program.json")).write_text(
        json.dumps(program_coverage,indent=2),encoding="utf-8")
    control_coverage=validate_raw_program(source,build,out,log,env,backend,control=True)
    (results/(source.name+"-"+out.parent.name+"-"+out.name+"-raw-control.json")).write_text(
        json.dumps(control_coverage,indent=2),encoding="utf-8")
    snapshot=validate_ucode_snapshot(source,build,out,log,env)
    env["XE_SHADER_BENCH_CSV"] = str(out / "prepare.csv")
    env["XE_SHADER_BENCH_NO_DUMP"] = "0"
    cpu = cpu_build(source, build, out, log, env)
    run([cpu, out], source, log, env, 240)
    if not WIN:
        validator = shutil.which("spirv-val")
        if not validator:
            raise ValueError("spirv-val is required by the Vulkan CI job")
        for module in sorted(out.glob("shader*.spv")):
            run([validator, "--target-env", "vulkan1.1", module], source, log, env, 30)
    if backend.startswith("vulkan"):
        exe = out / ("pso.exe" if WIN else "pso")
        if WIN:
            run(["cl", "/nologo", "/TC", "/MD", "/O2", "/W4", "/WX", "/D_CRT_SECURE_NO_WARNINGS",
                 "/I" + str(source / "third_party/Vulkan-Headers/include"), HERE / "shader_compile_vulkan_test.c",
                 "/Fo" + str(out / "pso.obj"), "/Fe" + str(exe)], source, log, env)
        else:
            run([env.get("CC", "cc"), "-std=c89", "-pedantic", "-Wno-long-long", "-Wdeclaration-after-statement",
                 "-Wall", "-Wextra", "-Werror", "-O2", "-isystem", source / "third_party/Vulkan-Headers/include",
                 HERE / "shader_compile_vulkan_test.c", "-ldl", "-o", exe], source, log, env)
        # Existing real guest vertex goldens execute separately from timed PSO creation.
        fixture_env = env.copy()
        fixture_env.update(FORWARD_FLOW_SOURCE=str(HERE / "core_shader_call_gpu_fixture.c"),
                           FORWARD_FLOW_BRIDGE=str(HERE / "forward_flow_bridge.cc"),
                           FORWARD_FLOW_OUT=str(out / "correctness"))
        if WIN:
            fixture_env["FORWARD_FLOW_BUILD"] = str(build)
            fixture_env["FORWARD_FLOW_LINK_FLAGS"] = "/CGTHREADS:" + str(JOBS)
            run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c",
                 str(source / "tools/tests/run_forward_flow_tests.cmd")], source, log, fixture_env)
        else:
            fixture_env["CXX"]=env.get("CXX","c++")+" -Wl,--strip-debug"
            run(["sh", source / "tools/tests/run_forward_flow_tests.sh"], source, log, fixture_env)
        numeric = out / ("numeric.exe" if WIN else "numeric")
        if WIN:
            run(["cl", "/nologo", "/TC", "/MD", "/O2", "/W4", "/WX", "/D_CRT_SECURE_NO_WARNINGS",
                 "/I" + str(source / "third_party/Vulkan-Headers/include"), HERE / "core_shader_call_vulkan_test.c",
                 "/Fo" + str(out / "numeric.obj"), "/Fe" + str(numeric)], source, log, env)
        else:
            run([env.get("CC", "cc"), "-std=c89", "-pedantic", "-Wno-long-long", "-Wall", "-Wextra", "-Werror",
                 "-O2", "-isystem", source / "third_party/Vulkan-Headers/include",
                 HERE / "core_shader_call_vulkan_test.c", "-ldl", "-o", numeric], source, log, env)
        run([numeric, out / "correctness"], source, log, env, 120)
        if backend == "vulkan-hardware":
            float_env=env.copy()
            float_env.update(FORWARD_FLOW_SOURCE=str(HERE / "core_float_rtz_gpu_fixture.c"),
                             FORWARD_FLOW_BRIDGE=str(HERE / "float_rtz_bridge.cc"),
                             FORWARD_FLOW_OUT=str(out / "float-correctness"))
            if WIN:
                float_env["FORWARD_FLOW_BUILD"]=str(build)
                float_env["FORWARD_FLOW_LINK_FLAGS"]="/CGTHREADS:"+str(JOBS)
                run([os.environ.get("COMSPEC","cmd.exe"),"/d","/c",
                     source / "tools/tests/run_forward_flow_tests.cmd"],source,log,float_env)
                float_exe=out / "float-numeric.exe"
                run(["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/D_CRT_SECURE_NO_WARNINGS",
                     "/DXE_VERTEX_TEST_CASES=704","/DXE_FLOAT_RTZ_GPU_TEST",
                     "/I"+str(source / "third_party/Vulkan-Headers/include"),HERE / "core_shader_call_vulkan_test.c",
                     "/Fo"+str(out / "float-numeric.obj"),"/Fe"+str(float_exe)],source,log,env)
            else:
                float_env["CXX"]=env.get("CXX","c++")+" -Wl,--strip-debug"
                run(["sh",source / "tools/tests/run_forward_flow_tests.sh"],source,log,float_env)
                float_exe=out / "float-numeric"
                run([env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
                     "-DXE_VERTEX_TEST_CASES=704","-DXE_FLOAT_RTZ_GPU_TEST","-isystem",source / "third_party/Vulkan-Headers/include",
                     HERE / "core_shader_call_vulkan_test.c","-ldl","-o",float_exe],source,log,env)
            run([float_exe,out / "float-correctness"],source,log,env,120)
        result={"cpu": cpu, "pso": exe, "source": source, "out": out}
        if program_coverage["vulkan_gpu"]:
            result["raw_program"]=out / "raw-program/native" / ("test.exe" if WIN else "test")
            result["raw_program_module"]=out / "raw-program/program.spv"
            if not result["raw_program"].is_file():raise ValueError("raw program executable missing")
        if control_coverage["vulkan_gpu"]:
            result["raw_control"]=out / "raw-control/native" / ("test.exe" if WIN else "test")
            result["raw_control_module"]=out / "raw-control/control.spv"
            if not result["raw_control"].is_file() or not result["raw_control_module"].is_file():
                raise ValueError("raw control executable/module missing")
        if backend == "vulkan-hardware":
            for profile in ["rtz","dot","frac","mad","softfrac","partialmad","softdot"]:
                result[profile]=out / profile
                result[profile].mkdir()
                profile_env=env.copy();profile_env["XE_SHADER_BENCH_"+profile.upper()+"_PROFILE"]="1"
                run([cpu,result[profile]],source,log,profile_env,240)
        result["ucode_snapshot"]=snapshot
        return result
    if not WIN or backend != "d3d12-warp":
        raise ValueError("unsupported backend/platform")
    inc = ["/I" + str(source / "src/xenia/gpu/dxil"), "/I" + str(source / "src/xenia/gpu/dxil/bytecode")]
    generator, runtime = out / "dxil.exe", out / "warp.exe"
    flags = ["cl", "/nologo", "/TC", "/MD", "/O2", "/W4", "/WX", "/wd4201", "/D_CRT_SECURE_NO_WARNINGS", "/DXE_SHADER_COMPILE_BENCH"]
    run([*flags, *inc, HERE / "dxil_guest_vertex_test.c", "/Fo" + str(out / "dxil.obj"),
         "/Fe" + str(generator), "/link", build / "obj/Windows/xenia-native-dxil.lib", "/CGTHREADS:" + str(JOBS)], source, log, env)
    run([*flags, HERE / "dxil_guest_vertex_runtime_test.c", "/Fo" + str(out / "warp.obj"),
         "/Fe" + str(runtime), "/link", "d3d12.lib", "dxgi.lib", "dxguid.lib"], source, log, env)
    for case in range(8):
        module = out / ("vertex%d.dxil" % case)
        run([generator, module, case], source, log, env, 60)
        run([runtime, module, case, 0, 0, 0, 0], source, log, env, 60)
    return {"cpu": cpu, "dxil": generator, "warp": runtime, "source": source, "out": out,"ucode_snapshot":snapshot}


def source_wait_alarms(command_processor, pipeline_cache):
    return dict(queue_drain_references=len(re.findall(
        r"\bAwaitPipelineCompletion\s*\(\s*\)", command_processor)),
        placeholder_creation_references=len(re.findall(
        r"\bEnsurePipelineCreatedWith(?:Interpreter)?Placeholder\s*\(", pipeline_cache)))


def vulkan_device_identity(log, expected_reports):
    """One report per timed profile; every invocation must use the same device."""
    identities=re.findall(r"^BENCH_DEVICE Vulkan .+$",log,re.MULTILINE)
    if expected_reports < 1 or len(identities)!=expected_reports:
        raise ValueError("device identity missing or duplicated")
    if len(set(identities))!=1:
        raise ValueError("different benchmark device/driver within sample")
    return identities[0]


def cpu_diagnostics(wall, cpu, expected_samples):
    """Supplementary process CPU only; never substitutes for elapsed-time gates."""
    if set(wall)!=set(cpu) or not wall:
        raise ValueError("missing or substituted process CPU cases")
    rows=[]
    for stage,which in sorted(wall):
        w,c=wall[(stage,which)],cpu[(stage,which)]
        if len(w)!=expected_samples or len(c)!=expected_samples:
            raise ValueError("missing process CPU samples")
        rows.append(dict(stage=stage,case=which,process_cpu=dict(median=statistics.median(c),
            p95=percentile(c,.95),p99=percentile(c,.99),maximum=max(c)),
            paired_samples=[dict(sample=i,wall_seconds=a,process_cpu_seconds=b) for i,(a,b) in enumerate(zip(w,c))]))
    return rows


def compare_timing_channels(wall, cpu, expected_samples):
    if set(wall)!=set(cpu) or not {"base","candidate"}<=set(wall):
        raise ValueError("missing or substituted timing roles")
    diagnostics={version:cpu_diagnostics(wall[version],cpu[version],expected_samples) for version in wall}
    rows=compare(wall["base"],wall["candidate"],expected_samples)
    anchor=compare(wall["anchor"],wall["candidate"],expected_samples) if "anchor" in wall else []
    return rows,anchor,diagnostics


def file_sha256(path):
    digest=hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b""):digest.update(chunk)
    return digest.hexdigest()


def validate_clock_fixture(exe, out):
    out.mkdir(parents=True,exist_ok=True)
    wall=out/"wall.csv";cpu=out/"cpu.csv"
    if wall.exists() or cpu.exists():raise ValueError("stale clock-fixture samples")
    env=os.environ.copy();env.update(XE_SHADER_BENCH_CSV=str(wall),XE_SHADER_BENCH_CPU_CSV=str(cpu))
    for _ in range(7):
        process=subprocess.Popen([str(exe)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE,text=True,env=env)
        try:
            if process.stdout.readline().strip()!="ready":raise ValueError("clock fixture not ready")
            time.sleep(.12)  # Controlled test stall; never a core/runtime pacing path.
            stdout,stderr=process.communicate(input="\n",timeout=10)
            if process.returncode:raise ValueError("clock fixture failed: "+stderr)
        finally:
            if process.poll() is None:process.kill();process.wait()
    w,c=read_samples(wall),read_samples(cpu)
    if set(w)!={("clock.blocked",0),("clock.busy",0)}:raise ValueError("clock fixture coverage missing")
    diagnostics=cpu_diagnostics(w,c,7)
    if min(w[("clock.blocked",0)])<.10 or max(c[("clock.blocked",0)])>.05:
        raise ValueError("blocked wall/process CPU separation failed")
    if min(c[("clock.busy",0)])<.04:raise ValueError("busy process CPU clock did not advance")
    # CPU diagnostics must not turn a real wall stall into a passing comparison.
    if not compare({("clock.blocked",0):[.001]*7},{("clock.blocked",0):w[("clock.blocked",0)]},7)[0]["failures"]:
        raise ValueError("wall stall was hidden")
    (out/"diagnostics.json").write_text(json.dumps(diagnostics,indent=2))


def read_samples(path):
    samples = {}
    with Path(path).open(newline="", encoding="utf-8") as source:
        for row in csv.reader(source):
            if len(row) != 3 or not re.fullmatch(r"[a-z0-9._]+", row[0]):
                raise ValueError("invalid measurement row")
            case, seconds = int(row[1]), float(row[2])
            if case < 0 or not math.isfinite(seconds) or seconds < 0:
                raise ValueError("invalid time/case")
            samples.setdefault((row[0], case), []).append(seconds)
    if not samples:
        raise ValueError("empty measurements")
    return samples


def percentile(values, fraction):
    values = sorted(values)
    position = (len(values)-1)*fraction
    lo = int(position)
    return values[lo] + (values[min(lo+1, len(values)-1)]-values[lo])*(position-lo)


def compare(base, candidate, expected_samples):
    if set(base) != set(candidate) or not base:
        raise ValueError("missing or changed measurement cases")
    result = []
    for key in sorted(base):
        a, b = base[key], candidate[key]
        if len(a) != expected_samples or len(b) != expected_samples or expected_samples < 5:
            raise ValueError("missing samples; minimum five per case")
        old, new = statistics.median(a), statistics.median(b)
        absolute = .001 if "translate" in key[0] else .002
        reasons = []
        if new-old > max(absolute, old*.10):
            reasons.append("median >10% and absolute noise floor")
        if percentile(b,.95)-percentile(a,.95) > max(.005, percentile(a,.95)*.25):
            reasons.append("p95 >25% and 5ms")
        if max(b)-max(a) > max(.010, max(a)*.50):
            reasons.append("max >50% and 10ms (regression or noisy runner; investigate)")
        result.append(dict(stage=key[0], case=key[1], samples=len(a),
            base=dict(median=old,p95=percentile(a,.95),p99=percentile(a,.99),maximum=max(a)),
            candidate=dict(median=new,p95=percentile(b,.95),p99=percentile(b,.99),maximum=max(b)), failures=reasons))
    return result


def expected_keys(backend):
    keys={("spirv.translate_batch4",i) for i in range(64)}
    if backend.startswith("vulkan"):
        keys.update((stage,i) for stage in ["vulkan.module","vulkan.vs_ps_pso"] for i in range(16))
        if backend=="vulkan-hardware":
            for profile in ["rtz","dot","frac","mad","softfrac","partialmad","softdot"]:
                keys.update(("spirv."+profile+"_translate_batch4",i) for i in range(64))
                keys.update((stage,i) for stage in ["vulkan."+profile+"_module","vulkan."+profile+"_vs_ps_pso"] for i in range(16))
    elif backend=="d3d12-warp":
        keys.update((stage,i) for stage in ["dxil.translate","d3d12.graphics_pso"] for i in range(8))
    else:
        raise ValueError("unknown backend")
    return keys


def spirv_manifest(directory):
    result = {}
    for path in sorted(directory.glob("shader*.spv")):
        with path.open("rb") as file, mmap.mmap(file.fileno(),0,access=mmap.ACCESS_READ) as data:
            if len(data)<20 or len(data)%4 or struct.unpack_from("<I",data)[0]!=0x07230203:
                raise ValueError("invalid SPIR-V")
            at, instructions = 20, 0
            while at<len(data):
                word=struct.unpack_from("<I",data,at)[0];size=(word>>16)*4
                if not size or at+size>len(data):
                    raise ValueError("invalid SPIR-V instruction")
                at+=size;instructions+=1
            result[path.name]=dict(bytes=len(data),instructions=instructions,sha256=hashlib.sha256(data).hexdigest())
    if len(result)!=64:
        raise ValueError("procedural corpus must contain all64 cases")
    return result


def compare_raw_program(raw, samples, prefix="raw_program."):
    """Gate comparable program kernels; a newly added kernel has no old timing."""
    if samples<5:raise ValueError("raw program needs at least five process pairs")
    expected={(prefix+stage,0) for stage in ("emit","module","compute_pso","compute_pso_warm")}
    expected.update((prefix+"submit_complete",i) for i in range(3))
    observed={}
    for version,data in raw.items():
        if set(data)!=expected or any(len(x)!=samples for x in data.values()):
            raise ValueError("missing raw program case/sample: "+version)
        observed[version]=[dict(stage=k[0],case=k[1],samples=len(v),
            median=statistics.median(v),p95=percentile(v,.95),p99=percentile(v,.99),
            maximum=max(v),outliers=sorted(enumerate(v),key=lambda x:x[1],reverse=True)[:5])
            for k,v in sorted(data.items())]
    first_use={(prefix+"compute_pso",0),(prefix+"submit_complete",0)}
    controlled={v:{k:x for k,x in data.items() if k not in first_use} for v,data in raw.items()}
    rows=compare(controlled["base"],controlled["candidate"],samples) if "base" in raw and "candidate" in raw else []
    anchor=compare(controlled["anchor"],controlled["candidate"],samples) if "anchor" in raw and "candidate" in raw else []
    removed=("base" in raw or "anchor" in raw) and "candidate" not in raw
    return dict(comparable=bool(rows),new_component_without_parent="candidate" in raw and "base" not in raw,
        missing_candidate=removed,cases=rows,anchor_cases=anchor,observed=observed,
        failure=removed or any(r["failures"] for r in rows+anchor),
        first_use_cache_status="UNVERIFIED: initial driver disk/internal state is not controlled",
        first_use_observations_only=sorted(first_use),
        controlled_condition="identical first PSO retained; explicit private application cache primed; submission0 completed",
        scope="prototype emission/module and primed PSO/warm completion gates; not cold-play or pure GPU time")


def compare_control_coverage(coverage, vulkan):
    candidate=coverage["candidate"];failures=[]
    if candidate["available"]:
        if not candidate["unit"]:failures.append("candidate unit coverage missing")
        if vulkan and not candidate["vulkan_gpu"]:failures.append("candidate Vulkan execution missing")
    for version in ("base","anchor"):
        if version not in coverage:continue
        previous=coverage[version]
        for feature in ("available","boolean_cf_supported","nonempty_loops_supported","predicate_cf_supported","comparison_alu_supported","lazy_source_supported"):
            if previous.get(feature,False) and not candidate.get(feature,False):
                failures.append(version+": removed "+feature)
        for feature in ("boolean_context_cases","loop_context_cases","predicate_context_cases","comparison_context_cases","lazy_source_context_cases"):
            if previous.get(feature,0)>candidate.get(feature,0):failures.append(version+": reduced "+feature)
    return dict(revisions=coverage,failures=failures)


def compare_ucode_snapshot(raw, samples):
    if samples<5:raise ValueError("snapshot workload needs at least five pairs")
    expected={("shader.ucode.snapshot_batch2000",i) for i in range(24)}
    for version,data in raw.items():
        if set(data)!=expected or any(len(x)!=samples for x in data.values()):
            raise ValueError("missing snapshot workload/sample: "+version)
    rows=compare(raw["base"],raw["candidate"],samples) if "base" in raw and "candidate" in raw else []
    anchor=compare(raw["anchor"],raw["candidate"],samples) if "anchor" in raw and "candidate" in raw else []
    removed=("base" in raw or "anchor" in raw) and "candidate" not in raw
    return dict(cases=rows,anchor_cases=anchor,available=bool(raw),failure=removed or any(r["failures"] for r in rows+anchor),
        scope="actual CPU Shader construction/destruction batches; not translation or gameplay")


def compare_ucode_coverage(coverage):
    failures=[];candidate=coverage.get("candidate")
    if coverage and not candidate:failures.append("candidate snapshot coverage missing")
    if candidate:
        if not candidate.get("native") or candidate.get("cases")!=96:failures.append("candidate snapshot native coverage missing")
        if candidate.get("owner_available") and not candidate.get("owner_unit"):failures.append("candidate C89 owner unit missing")
        for version in ("base","anchor"):
            if coverage.get(version,{}).get("owner_available") and not candidate.get("owner_available"):
                failures.append(version+": removed C89 owner coverage")
    return dict(revisions=coverage,failures=failures)


def validate_valid_workload(log, components):
    counts={"raw_program":(363,380),"raw_control":(315,428)}
    expected=[counts[x] for x in counts if x in components]
    actual=[tuple(map(int,x)) for x in re.findall(r"^PROFILE_VALID_WORK active=(\d+) inactive=(\d+);",log,re.MULTILINE)]
    if sorted(actual)!=sorted(expected):
        raise ValueError("valid-work profile missing or substituted records")


def run_comparison(args):
    work=Path(args.out).resolve();results=work/"results";results.mkdir(parents=True,exist_ok=True)
    if any(results.glob("*.csv")):
        raise ValueError("use a fresh output directory; stale samples are not reusable")
    if args.self_check and args.base!=args.head:
        raise ValueError("self-check must use equal reference labels")
    env=os.environ.copy()
    env.pop("XE_ALU_PROGRAM_VALID_WORK",None)
    if args.backend=="vulkan-software":
        icds=sorted(Path("/usr/share/vulkan/icd.d").glob("lvp*.json"))
        if not icds:raise ValueError("Lavapipe ICD missing; no silent hardware substitution")
        env.update(VK_ICD_FILENAMES=str(icds[0]),MESA_SHADER_CACHE_DISABLE="true")
    env["XDG_CACHE_HOME"]=str(work/"private-driver-cache")
    os.environ.update(env)
    manifest=dict(base=args.base,head=args.head,backend=args.backend,samples=args.samples,
        host=platform.platform(),cpu=platform.processor(),jobs=JOBS,
        cache="null explicit VkPipelineCache; software Mesa disk cache disabled" if args.backend=="vulkan-software" else
              "no application cache; driver internal/disk cache NOT certified cold",
        coverage="procedural translation and PSO tests; not full gameplay cold/warm signoff",
        anchor=os.environ.get("SHADER_PERF_ANCHOR") or "UNSET: no last-known-good ratchet certified")
    manifest["toolchain"]={}
    manifest["harness_revision"]=git("rev-parse","HEAD")
    manifest["harness_sha256"]={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in
        ["core_shader_compile_fixture.c","shader_call_fixture.h","shader_compile_bridge.cc",
         "shader_compile_vulkan_test.c","shader_compile_bench.h","shader_compile_ci.py",
         "vulkan_test_context.h","core_shader_call_gpu_fixture.c","core_shader_call_vulkan_test.c",
         "core_float_rtz_gpu_fixture.c","float_rtz_bridge.cc","shader_xfb_fixture.h","core_shader_clock_test.c",
         "core_shader_loop_test.c","core_shader_loop_gpu_fixture.c","shader_loop_accuracy_bridge.cc",
         "shader_control_program_fixture.h","core_alu_program_vulkan_test.c","alu_program_bridge.cc",
         "core_shader_ucode_test.c","core_shader_ucode_native_test.c","shader_ucode_bridge.cc"]}
    manifest["corpus_contract"]=dict(version=8,cases=64,hardware_rtz_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    hardware_dot_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    hardware_frac_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    hardware_mad_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    software_frac_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    partial_mad_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    software_dot_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    hardware_alu_cases=704 if args.backend=="vulkan-hardware" else 0,alu_additions=[32,96,256],
                                    profiles="VS/PS, native/portable swizzle,1x/4x",batch=4)
    manifest["raw_valid_work_profile"]=dict(version=1,active_program=363,active_control=315,
        inactive="early bounds rejection; no payload compaction; mixed fault coverage retained")
    for name,command in [("compiler",["cl","/Bv"] if WIN else [env.get("CC","cc"),"--version"]),
                         ("cmake",["cmake","--version"])]:
        tool=subprocess.run(command,capture_output=True,text=True,errors="replace")
        manifest["toolchain"][name]=tool.stdout+tool.stderr
    manifest["sdk"]={name:env.get(name,"") for name in ["WindowsSDKVersion","VCToolsVersion"]}
    if WIN:
        warp=subprocess.run(["powershell","-NoProfile","-Command",
            "Get-Item $env:SystemRoot/System32/d3d12.dll,$env:SystemRoot/System32/d3d10warp.dll | Select-Object Name,@{n='Version';e={$_.VersionInfo.FileVersion}} | ConvertTo-Json"],
            capture_output=True,text=True,check=True)
        manifest["warp_libraries"]=json.loads(warp.stdout)
    (results/"environment.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    versions=[("base",args.base),("candidate",args.head)]
    anchor=os.environ.get("SHADER_PERF_ANCHOR")
    if anchor and not args.self_check:
        versions.append(("anchor",git("rev-parse","--verify",anchor+"^{commit}")))
    builds={}
    for version,ref in versions:
        if args.self_check and version=="candidate":
            builds[version]=builds["base"]
            continue
        if args.self_check:
            source=Path(args.self_check).resolve()
            build=args.reuse_build
        else:
            source=work/("source-"+version)
            run(["git","worktree","add","--detach",source,ref],ROOT,results/"checkout.log")
            build=None
        builds[version]=prepare(source,work/version,results,args.backend,build)
        if not args.self_check:
            # Only the just-created private worktree's build tree is disposable.
            # Fixtures are statically linked and live outside it. Never touch a
            # supplied/reused build or a directory outside this task workspace.
            target=(source/"build").resolve()
            if source.resolve().parent!=work or target.parent!=source.resolve() or source.resolve()==ROOT:
                raise ValueError("unsafe private build cleanup target")
            if target.exists():shutil.rmtree(target)
    control_coverage={version:json.loads((results/(b["source"].name+"-"+b["out"].parent.name+"-"+b["out"].name+"-raw-control.json")).read_text())
                      for version,b in builds.items()}
    control_coverage_report=compare_control_coverage(control_coverage,args.backend.startswith("vulkan"))
    manifest["timing_contract"]=dict(version=2,gating="elapsed wall time only",
        process_cpu="supplementary per-process user+kernel time; may exceed wall time with parallel workers",
        limits="CPU/wall separation alone does not identify preemption, waits or GPU/service work",
        precision="Windows process CPU accounting may quantize short intervals; zero is not zero work")
    manifest["clock_controls"]={version:"clock-checks/"+b["out"].parent.name+"-"+b["out"].name for version,b in builds.items()}
    manifest["prepared_binaries"]={version:{name:dict(sha256=file_sha256(b[name]),bytes=b[name].stat().st_size)
        for name in ["cpu","pso","dxil","warp","raw_program","raw_program_module","raw_control","raw_control_module","ucode_snapshot"] if name in b} for version,b in builds.items()}
    (results/"environment.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    ir={version:spirv_manifest(b["out"]) for version,b in builds.items()}
    for version,b in builds.items():
        for profile in ["rtz","dot","frac","mad","softfrac","partialmad","softdot"]:
            if profile in b:
                ir[version].update({profile+"/"+name:value for name,value in spirv_manifest(b[profile]).items()})
    new_ir=ir["candidate"]
    ir_failures=[version+":"+name for version,old_ir in ir.items() if version!="candidate" for name in old_ir
                 if new_ir[name]["bytes"]-old_ir[name]["bytes"]>max(4096,old_ir[name]["bytes"]*.10)]
    (results/"corpus.json").write_text(json.dumps(ir,indent=2),encoding="utf-8")
    for sample in range(args.samples):
        order=[x[0] for x in versions]
        if sample%2:order.reverse()
        for version in order:
            b=builds[version];e=env.copy();e["XE_SHADER_BENCH_CSV"]=str(results/(version+".csv"))
            e["XE_SHADER_BENCH_NO_DUMP"]="1"
            e["XE_SHADER_BENCH_CPU_CSV"]=str(results/(version+"-cpu.csv"))
            log=results/(version+"-sample%d.log"%sample)
            run([b["cpu"],b["out"]],b["source"],log,e,240)
            if "pso" in b:
                run([b["pso"],b["out"]],b["source"],log,e,240)
                for profile in ["rtz","dot","frac","mad","softfrac","partialmad","softdot"]:
                    if profile in b:
                        profile_env=e.copy();profile_env["XE_SHADER_BENCH_"+profile.upper()+"_PROFILE"]="1"
                        run([b["cpu"],b[profile]],b["source"],log,profile_env,240)
                        run([b["pso"],b[profile]],b["source"],log,profile_env,240)
            else:
                for case in range(8):
                    module=b["out"]/("vertex%d.dxil"%case)
                    run([b["dxil"],module,case],b["source"],log,e,60)
                    run([b["warp"],module,case,0,0,0,0],b["source"],log,e,60)
            for component in ("raw_program","raw_control"):
                if component not in b:continue
                filename=component.replace("_","-")
                program_env=e.copy()
                program_env["XE_ALU_PROGRAM_TIMING_ONLY"]="1"
                program_env["XE_SHADER_BENCH_CSV"]=str(results/(version+"-"+filename+".csv"))
                program_env["XE_SHADER_BENCH_CPU_CSV"]=str(results/(version+"-"+filename+"-cpu.csv"))
                program_env["XE_ALU_PROGRAM_DUMP"]=str(results/(version+"-"+filename+"-sample%d.spv"%sample))
                run([b[component]],b["source"],log,program_env,120)
                valid_env=program_env.copy();valid_env["XE_ALU_PROGRAM_VALID_WORK"]="1"
                valid_env["XE_SHADER_BENCH_CSV"]=str(results/(version+"-"+filename+"-valid.csv"))
                valid_env["XE_SHADER_BENCH_CPU_CSV"]=str(results/(version+"-"+filename+"-valid-cpu.csv"))
                valid_env["XE_ALU_PROGRAM_DUMP"]=str(results/(version+"-"+filename+"-valid-sample%d.spv"%sample))
                run([b[component]],b["source"],log,valid_env,120)
            if "ucode_snapshot" in b:
                snapshot_env=e.copy()
                snapshot_env["XE_SHADER_BENCH_CSV"]=str(results/(version+"-ucode.csv"))
                snapshot_env["XE_SHADER_BENCH_CPU_CSV"]=str(results/(version+"-ucode-cpu.csv"))
                run([b["ucode_snapshot"]],b["source"],log,snapshot_env,120)
    raw={version:read_samples(results/(version+".csv")) for version in builds}
    for version,measurements in raw.items():
        if set(measurements)!=expected_keys(args.backend):
            raise ValueError("missing or substituted expected stage cases: "+version)
    cpu={version:read_samples(results/(version+"-cpu.csv")) for version in builds}
    rows,anchor_rows,cpu_rows=compare_timing_channels(raw,cpu,args.samples)
    snapshot_raw={v:read_samples(results/(v+"-ucode.csv")) for v,b in builds.items() if "ucode_snapshot" in b}
    snapshot_report=compare_ucode_snapshot(snapshot_raw,args.samples)
    snapshot_coverage={v:json.loads((b["ucode_snapshot"].parent/"coverage.json").read_text()) for v,b in builds.items() if "ucode_snapshot" in b}
    snapshot_report["coverage"]=compare_ucode_coverage(snapshot_coverage)
    snapshot_report["failure"]|=bool(snapshot_report["coverage"]["failures"])
    snapshot_cpu={v:read_samples(results/(v+"-ucode-cpu.csv")) for v in snapshot_raw}
    snapshot_report["process_cpu_diagnostics"]={v:cpu_diagnostics(snapshot_raw[v],snapshot_cpu[v],args.samples) for v in snapshot_raw}
    program_raw={v:read_samples(results/(v+"-raw-program.csv")) for v,b in builds.items() if "raw_program" in b}
    program_report=compare_raw_program(program_raw,args.samples)
    program_cpu={v:read_samples(results/(v+"-raw-program-cpu.csv")) for v in program_raw}
    program_report["process_cpu_observed"]=compare_raw_program(program_cpu,args.samples)["observed"]
    program_report["module_growth_failures"]=[]
    if "candidate" in program_raw:
        new_bytes=manifest["prepared_binaries"]["candidate"]["raw_program_module"]["bytes"]
        for version in ("base","anchor"):
            if version not in program_raw:continue
            old_bytes=manifest["prepared_binaries"][version]["raw_program_module"]["bytes"]
            if new_bytes-old_bytes>max(4096,old_bytes*.10):program_report["module_growth_failures"].append(version)
        program_report["failure"]|=bool(program_report["module_growth_failures"])
    control_raw={v:read_samples(results/(v+"-raw-control.csv")) for v,b in builds.items() if "raw_control" in b}
    control_report=compare_raw_program(control_raw,args.samples,"raw_control.")
    control_cpu={v:read_samples(results/(v+"-raw-control-cpu.csv")) for v in control_raw}
    control_report["process_cpu_observed"]=compare_raw_program(control_cpu,args.samples,"raw_control.")["observed"]
    control_report["module_growth_failures"]=[]
    if "candidate" in control_raw:
        new_bytes=manifest["prepared_binaries"]["candidate"]["raw_control_module"]["bytes"]
        for version in ("base","anchor"):
            if version not in control_raw:continue
            old_bytes=manifest["prepared_binaries"][version]["raw_control_module"]["bytes"]
            if new_bytes-old_bytes>max(4096,old_bytes*.10):control_report["module_growth_failures"].append(version)
        control_report["failure"]|=bool(control_report["module_growth_failures"])
    valid_reports={}
    for component in ("raw_program","raw_control"):
        filename=component.replace("_","-")+"-valid"
        valid_raw={v:read_samples(results/(v+"-"+filename+".csv")) for v,b in builds.items() if component in b}
        valid_cpu={v:read_samples(results/(v+"-"+filename+"-cpu.csv")) for v in valid_raw}
        valid_reports[component]=compare_raw_program(valid_raw,args.samples,component+".")
        valid_reports[component]["process_cpu_observed"]=compare_raw_program(valid_cpu,args.samples,component+".")["observed"]
        valid_reports[component]["workload"]="successful primary records unchanged; negative lanes rejected by empty program bounds before VM execution; full mixed gates retained"
    identities=[]
    if args.backend.startswith("vulkan"):
        for version in builds:
            for sample in range(args.samples):
                log=(results/(version+"-sample%d.log"%sample)).read_text(encoding="utf-8",errors="replace")
                expected_reports=1+sum(profile in builds[version] for profile in ["rtz","dot","frac","mad","softfrac","partialmad","softdot"])
                identities.append(vulkan_device_identity(log,expected_reports))
                for component in ("raw_program","raw_control"):
                    if component not in builds[version]:continue
                    device_label=component.upper()+"_DEVICE"
                    program_id=re.findall(r"^"+device_label+r" Vulkan .+$",log,re.MULTILINE)
                    if len(program_id)!=1 or program_id[0].replace(device_label,"BENCH_DEVICE",1)!=identities[-1]:
                        raise ValueError("raw program used missing/different device or driver")
                    actual=results/(version+"-"+component.replace("_","-")+"-sample%d.spv"%sample)
                    if file_sha256(actual)!=manifest["prepared_binaries"][version][component+"_module"]["sha256"]:
                        raise ValueError("raw program emitted a different module during timing")
                    valid_label=device_label+"_VALID"
                    valid_id=re.findall(r"^"+valid_label+r" Vulkan .+$",log,re.MULTILINE)
                    if len(valid_id)!=1 or valid_id[0].replace(valid_label,"BENCH_DEVICE",1)!=identities[-1]:
                        raise ValueError("valid-work profile used missing/different device or driver")
                    actual=results/(version+"-"+component.replace("_","-")+"-valid-sample%d.spv"%sample)
                    if file_sha256(actual)!=manifest["prepared_binaries"][version][component+"_module"]["sha256"]:
                        raise ValueError("valid-work profile emitted a different module")
                validate_valid_workload(log,builds[version])
        if len(set(identities))!=1:raise ValueError("different benchmark device/driver")
        software=any(x in identities[0].lower() for x in ["llvmpipe","lavapipe"])
        if software != (args.backend=="vulkan-software"):raise ValueError("wrong software/hardware device")
    source_alarms={}
    for version,b in builds.items():
        cp=(b["source"]/"src/xenia/gpu/vulkan/vulkan_command_processor.cc").read_text(encoding="utf-8")
        pc=(b["source"]/"src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc").read_text(encoding="utf-8")
        source_alarms[version]=source_wait_alarms(cp,pc)
    alarm_failures=[name for name,n in source_alarms["base"].items() if source_alarms["candidate"][name]>n]
    driver_identity={}
    if args.backend.startswith("vulkan"):
        driver_identity={version:dict(
            executable_identical_to_candidate=manifest["prepared_binaries"][version]["pso"]["sha256"]==
                manifest["prepared_binaries"]["candidate"]["pso"]["sha256"],
            module_manifest_identical_to_candidate=ir[version]==ir["candidate"]) for version in builds}
    report=dict(environment=manifest,device=identities,cases=rows,anchor_cases=anchor_rows,
                driver_workload_identity=driver_identity,
                process_cpu_diagnostics=cpu_rows,
                source_alarms=source_alarms,source_alarm_limit="textual alarm only; not dynamic critical-thread counters",
                ir_growth_failures=ir_failures,source_alarm_failures=alarm_failures,
                raw_program=program_report,raw_control=control_report,raw_control_coverage=control_coverage_report,raw_valid_work=valid_reports,ucode_snapshot=snapshot_report,
                success=not ir_failures and not alarm_failures and not control_coverage_report["failures"] and not snapshot_report["failure"] and not program_report["failure"] and not control_report["failure"] and not any(v["failure"] for v in valid_reports.values()) and not any(x["failures"] for x in rows+anchor_rows),
                self_check=bool(args.self_check))
    (results/"comparison.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    summary="# Shader compilation comparison\n\n%s -> %s (%s)\n\n"%(args.base,args.head,args.backend)
    summary+="**%s** across %d procedural stage/case metrics.\n\n"%("PASS" if report["success"] else "FAIL / investigate",len(rows))
    summary+="This is NOT cold-play gameplay signoff. Driver-cache status and missing anchor are recorded in environment.json.\n\n"
    if snapshot_report["available"]:
        summary+="Actual Shader snapshot construction/destruction: 24 paired batch cases with source-mutation/endian correctness checks; CPU ownership cost only, not gameplay fluidity.\n\n"
    if any(v["observed"] for v in valid_reports.values()):
        summary+="Companion valid-work profiles keep successful records unchanged and reject inactive negative lanes before VM execution. Separate elapsed/CPU samples, identity checks and unchanged timing gates supplement the full mixed-corpus gates; they do not replace fault coverage or establish pure GPU time.\n\n"
    if program_report["observed"]:
        status="paired primed-cache/warm-execution timing gates apply" if program_report["comparable"] else "no historical parent implementation; timing is UNCOMPARED"
        if program_report["missing_candidate"]:status="candidate kernel removed: FAIL"
        summary+="Raw program prototype: "+status+". First-use cache conditions remain UNVERIFIED; all first-use samples are retained. Rendering integration and full shader coverage remain absent.\n\n"
    if control_report["observed"]:
        status="comparable primed-cache/warm-execution gates apply" if control_report["comparable"] else "new component; no historical parent timing"
        if control_report["missing_candidate"]:status="candidate kernel removed: FAIL"
        summary+="Raw control-flow prototype: "+status+". First-use cache conditions remain UNVERIFIED. Bounded execution and missing stages/fetch do not qualify as a renderer fallback.\n\n"
    summary+="Process CPU samples are supplementary diagnostics; elapsed-time thresholds remain authoritative.\n\n"
    summary+="Parallel workers can consume more process CPU than elapsed time; short Windows CPU intervals may quantize to zero.\n\n"
    summary+="|Stage|Case|Parent wall ms|Candidate wall ms|Parent CPU ms|Candidate CPU ms|Result|\n|---|---:|---:|---:|---:|---:|---|\n"
    cpu_lookup={version:{(x["stage"],x["case"]):x["process_cpu"]["median"] for x in data} for version,data in cpu_rows.items()}
    for row in rows:
        key=(row["stage"],row["case"])
        summary+="|%s|%d|%.3f|%.3f|%.3f|%.3f|%s|\n"%(row["stage"],row["case"],row["base"]["median"]*1000,
            row["candidate"]["median"]*1000,cpu_lookup["base"][key]*1000,cpu_lookup["candidate"][key]*1000,
            "; ".join(row["failures"]) or "within thresholds")
    failed_rows=[row for row in rows if row["failures"]]
    if failed_rows:
        summary+="\nCandidate worst-wall samples (paired CPU is diagnostic; failures remain):\n\n"
        summary+="|Stage|Case|Sample|Parent wall ms|Candidate wall ms|Parent CPU ms|Candidate CPU ms|\n|---|---:|---:|---:|---:|---:|---:|\n"
        for row in failed_rows:
            key=(row["stage"],row["case"])
            which=max(range(args.samples),key=lambda i:raw["candidate"][key][i])
            summary+="|%s|%d|%d|%.3f|%.3f|%.3f|%.3f|\n"%(key[0],key[1],which,
                raw["base"][key][which]*1000,raw["candidate"][key][which]*1000,
                cpu["base"][key][which]*1000,cpu["candidate"][key][which]*1000)
    if ir_failures:summary+="\nIR growth failures: "+", ".join(ir_failures)+"\n"
    if alarm_failures:summary+="\nSource alarm increases: "+", ".join(alarm_failures)+"\n"
    if control_coverage_report["failures"]:summary+="\nControl coverage failures: "+", ".join(control_coverage_report["failures"])+"\n"
    if anchor_rows:
        summary+="\nPinned anchor: %d failing case metrics.\n"%sum(bool(x["failures"]) for x in anchor_rows)
    (results/"summary.md").write_text(summary,encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"],"a",encoding="utf-8") as file:file.write(summary)
    print(summary.splitlines()[4])
    return 0 if report["success"] else 1


def main():
    parser=argparse.ArgumentParser();sub=parser.add_subparsers(dest="command",required=True)
    sub.add_parser("matrix")
    run_parser=sub.add_parser("run")
    for arg in ["base","head","out","backend"]:run_parser.add_argument("--"+arg,required=True)
    run_parser.add_argument("--samples",type=int,default=7)
    run_parser.add_argument("--self-check",help="local harness validation only; not revision acceptance")
    run_parser.add_argument("--reuse-build",help="self-check only: existing native CMake build")
    args=parser.parse_args()
    if args.command=="matrix":
        event=json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
        selected=select_commits(os.environ["GITHUB_EVENT_NAME"],os.environ["GITHUB_SHA"],event.get("before"),
                                [x["id"] for x in event.get("commits",[])])
        matrix=dict(include=[dict(x,backend=b,runner=r) for x in selected for b,r in
            [("vulkan-software","ubuntu-24.04"),("d3d12-warp","windows-2022")]])
        encoded=json.dumps(matrix,separators=(",",":"));print(encoded)
        with open(os.environ["GITHUB_OUTPUT"],"a",encoding="utf-8") as file:
            file.write("matrix="+encoded+"\ncommits="+json.dumps(dict(include=selected))+"\n")
        return 0
    if args.samples<5:raise ValueError("at least five paired samples required")
    if args.reuse_build and not args.self_check:raise ValueError("reuse-build requires self-check")
    return run_comparison(args)


if __name__=="__main__":
    sys.exit(main())
