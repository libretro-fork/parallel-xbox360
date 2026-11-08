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


def prepare(source, out, results, backend, reuse_build=None):
    env = os.environ.copy()
    build = Path(reuse_build).resolve() if reuse_build else source / "build/shader-perf-core"
    log = results / (source.name + "-build.log")
    if not reuse_build:
        if WIN:
            run(["cmake", "-S", source, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl"], source, log, env)
            run(["cmake", "--build", build, "--target", "xenia-libretro", "--parallel", JOBS], source, log, env)
        else:
            run(["make", "-j" + str(JOBS)], source, log, env)
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
                     "/DXE_VERTEX_TEST_CASES=384","/DXE_FLOAT_RTZ_GPU_TEST",
                     "/I"+str(source / "third_party/Vulkan-Headers/include"),HERE / "core_shader_call_vulkan_test.c",
                     "/Fo"+str(out / "float-numeric.obj"),"/Fe"+str(float_exe)],source,log,env)
            else:
                float_env["CXX"]=env.get("CXX","c++")+" -Wl,--strip-debug"
                run(["sh",source / "tools/tests/run_forward_flow_tests.sh"],source,log,float_env)
                float_exe=out / "float-numeric"
                run([env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
                     "-DXE_VERTEX_TEST_CASES=384","-DXE_FLOAT_RTZ_GPU_TEST","-isystem",source / "third_party/Vulkan-Headers/include",
                     HERE / "core_shader_call_vulkan_test.c","-ldl","-o",float_exe],source,log,env)
            run([float_exe,out / "float-correctness"],source,log,env,120)
        result={"cpu": cpu, "pso": exe, "source": source, "out": out}
        if backend == "vulkan-hardware":
            for profile in ["rtz","dot"]:
                result[profile]=out / profile
                result[profile].mkdir()
                profile_env=env.copy();profile_env["XE_SHADER_BENCH_"+profile.upper()+"_PROFILE"]="1"
                run([cpu,result[profile]],source,log,profile_env,240)
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
    return {"cpu": cpu, "dxil": generator, "warp": runtime, "source": source, "out": out}


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
            for profile in ["rtz","dot"]:
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


def run_comparison(args):
    work=Path(args.out).resolve();results=work/"results";results.mkdir(parents=True,exist_ok=True)
    if (results/"base.csv").exists() or (results/"candidate.csv").exists():
        raise ValueError("use a fresh output directory; stale samples are not reusable")
    if args.self_check and args.base!=args.head:
        raise ValueError("self-check must use equal reference labels")
    env=os.environ.copy()
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
         "core_float_rtz_gpu_fixture.c","float_rtz_bridge.cc","shader_xfb_fixture.h"]}
    manifest["corpus_contract"]=dict(version=3,cases=64,hardware_rtz_cases=64 if args.backend=="vulkan-hardware" else 0,
                                    hardware_dot_cases=64 if args.backend=="vulkan-hardware" else 0,alu_additions=[32,96,256],
                                    profiles="VS/PS, native/portable swizzle,1x/4x",batch=4)
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
    ir={version:spirv_manifest(b["out"]) for version,b in builds.items()}
    for version,b in builds.items():
        for profile in ["rtz","dot"]:
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
            log=results/(version+"-sample%d.log"%sample)
            run([b["cpu"],b["out"]],b["source"],log,e,240)
            if "pso" in b:
                run([b["pso"],b["out"]],b["source"],log,e,240)
                for profile in ["rtz","dot"]:
                    if profile in b:
                        profile_env=e.copy();profile_env["XE_SHADER_BENCH_"+profile.upper()+"_PROFILE"]="1"
                        run([b["cpu"],b[profile]],b["source"],log,profile_env,240)
                        run([b["pso"],b[profile]],b["source"],log,profile_env,240)
            else:
                for case in range(8):
                    module=b["out"]/("vertex%d.dxil"%case)
                    run([b["dxil"],module,case],b["source"],log,e,60)
                    run([b["warp"],module,case,0,0,0,0],b["source"],log,e,60)
    raw={version:read_samples(results/(version+".csv")) for version in builds}
    for version,measurements in raw.items():
        if set(measurements)!=expected_keys(args.backend):
            raise ValueError("missing or substituted expected stage cases: "+version)
    rows=compare(raw["base"],raw["candidate"],args.samples)
    anchor_rows=compare(raw["anchor"],raw["candidate"],args.samples) if "anchor" in builds else []
    identities=[]
    if args.backend.startswith("vulkan"):
        for version in builds:
            for sample in range(args.samples):
                log=(results/(version+"-sample%d.log"%sample)).read_text(encoding="utf-8",errors="replace")
                expected_reports=1+sum(profile in builds[version] for profile in ["rtz","dot"])
                identities.append(vulkan_device_identity(log,expected_reports))
        if len(set(identities))!=1:raise ValueError("different benchmark device/driver")
        software=any(x in identities[0].lower() for x in ["llvmpipe","lavapipe"])
        if software != (args.backend=="vulkan-software"):raise ValueError("wrong software/hardware device")
    source_alarms={}
    for version,b in builds.items():
        cp=(b["source"]/"src/xenia/gpu/vulkan/vulkan_command_processor.cc").read_text(encoding="utf-8")
        pc=(b["source"]/"src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc").read_text(encoding="utf-8")
        source_alarms[version]=source_wait_alarms(cp,pc)
    alarm_failures=[name for name,n in source_alarms["base"].items() if source_alarms["candidate"][name]>n]
    report=dict(environment=manifest,device=identities,cases=rows,anchor_cases=anchor_rows,
                source_alarms=source_alarms,source_alarm_limit="textual alarm only; not dynamic critical-thread counters",
                ir_growth_failures=ir_failures,source_alarm_failures=alarm_failures,
                success=not ir_failures and not alarm_failures and not any(x["failures"] for x in rows+anchor_rows),
                self_check=bool(args.self_check))
    (results/"comparison.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    summary="# Shader compilation comparison\n\n%s -> %s (%s)\n\n"%(args.base,args.head,args.backend)
    summary+="**%s** across %d procedural stage/case metrics.\n\n"%("PASS" if report["success"] else "FAIL / investigate",len(rows))
    summary+="This is NOT cold-play gameplay signoff. Driver-cache status and missing anchor are recorded in environment.json.\n\n"
    summary+="|Stage|Case|Parent median ms|Candidate median ms|Result|\n|---|---:|---:|---:|---|\n"
    for row in rows:
        summary+="|%s|%d|%.3f|%.3f|%s|\n"%(row["stage"],row["case"],row["base"]["median"]*1000,
            row["candidate"]["median"]*1000,"; ".join(row["failures"]) or "within thresholds")
    if ir_failures:summary+="\nIR growth failures: "+", ".join(ir_failures)+"\n"
    if alarm_failures:summary+="\nSource alarm increases: "+", ".join(alarm_failures)+"\n"
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
