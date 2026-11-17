"""Conditional ALU correctness orchestration, shared identically by both revisions.
Production algorithms and fixture drivers are C89. No gameplay/performance claim.
Only validate_conditional is wired to CI; shared private fixture adapters retain
rounding/selection dispatch for parity with the already validated branch runner.
"""
import math,os,re,shutil,subprocess,time,zipfile
from pathlib import Path
from shader_compile_ci import HERE,WIN,JOBS,run,file_sha256
CONDITIONAL_DXIL_PHASES=("setup","device","root_upload","pso","record","submit_wait","readback_check","execute_release","teardown")

def validate_selection_log(text,fixed):
    expected=0 if fixed else 648
    marker=f"SELECTION_OBSERVATION cases=648 CPU_lane_disagreements={expected}; compare=FTZ selected=raw"
    if text.splitlines().count(marker)!=1 or text.splitlines().count("SELECTION_MODULE_CONTROLS signed_zero_inf_nan_preserve=1 FTZ=0")!=1:
        raise ValueError("missing or substituted native selection observation")
    return dict(available=True,fixed=fixed,cases=648,cpu_lane_disagreements=expected,
                legacy_known_failure=not fixed,gpu=False,unit=False)

def validate_rounding_native_log(text,fixed):
    expected=0 if fixed else 286
    marker=f"ROUNDING_INPUT_DATAFLOW programs=384 operations=429 missing={expected}; immutable native words"
    if text.splitlines().count(marker)!=1 or text.splitlines().count("PASS: 384 native rounding fixtures and literal outputs")!=1:
        raise ValueError("rounding CPU/literal or explicit input-flush dataflow missing")
    return dict(available=True,fixed=fixed,cases=384,input_operations=429,input_missing=expected,
                cpu_literal_equal=True,legacy_known_failure=not fixed,gpu=False,unit=False)

def validate_conditional_native_log(text,fixed):
    differences=0 if fixed else 88
    marker=f"CONDITIONAL_AUDIT programs=84 CPU_lane_differences={differences}; raw payload literals, last12 probe condition FTZ"
    if text.splitlines().count(marker)!=1:raise ValueError("conditional native/CPU audit incomplete or substituted")
    return dict(available=True,fixed=fixed,cases=84,cpu_lane_disagreements=differences,
                cpu_literal_equal=fixed,legacy_known_failure=not fixed,gpu=False,unit=False,unit_scalar=False)

def validate_rounding_legacy_log(text):
    rows=re.findall(r"^ROUNDING_LEGACY_OBSERVATION allowed_cases=6 differing_case_mask=(\d+) differing_lanes=(\d+); not a correct historical reference$",text,re.MULTILINE)
    if len(rows)!=1:raise ValueError("legacy rounding observation missing or duplicated")
    mask,lanes=map(int,rows[0])
    if not 0<=mask<64 or lanes!=mask.bit_count()*12:raise ValueError("partial or substituted legacy rounding work")
    return dict(legacy_differing_case_mask=mask,legacy_differing_lanes=lanes)

def validate_rounding_dxil_log(text,hardware=False):
    markers=["DXIL_ROUNDING_CASES scalar=96 vector=96 NaN_class_only=1 FTZ_expected=1",
             "D3D12 WARP pipeline and execution: 0x00000000"]
    if hardware:markers.append("D3D12 hardware pipeline and execution: 0x00000000")
    if any(text.splitlines().count(marker)!=1 for marker in markers) or text.splitlines().count("Native DXIL execution: 2984 words matched.")!=(2 if hardware else 1):
        raise ValueError("DXIL rounding execution missing or substituted")
    return dict(available=True,scalar_cases=96,vector_cases=96,matched_words=2984,
                warp=True,hardware=hardware,scope="native ALU with vertex arithmetic flag, compute execution; not full vertex-stage rendering")

def validate_conditional_dxil_log(text,hardware=False,legacy=False):
    markers=["DXIL_CONDITIONAL_CASES vector=84 raw_payload=1 FTZ_condition_expected=1",
             "DXIL_ROUNDING_CASES scalar=96 vector=96 NaN_class_only=1 FTZ_expected=1",
             "D3D12 WARP pipeline and execution: 0x00000000"]
    if hardware:markers.append("D3D12 hardware pipeline and execution: 0x00000000")
    count=2 if hardware else 1
    matched=text.splitlines().count("Native DXIL execution: 3656 words matched.")
    observed=text.splitlines().count("OBSERVED: native DXIL 3656 words checked; conditional differences retained")
    observations=re.findall(r"^DXIL_CONDITIONAL_LEGACY_OBSERVATION case_mask=(\d+) differing_lanes=(\d+); not a correct historical reference$",text,re.MULTILINE)
    if any(text.splitlines().count(x)!=1 for x in markers) or matched!=(0 if legacy else count) or observed!=(count if legacy else 0) or len(observations)!=(count if legacy else 0):
        raise ValueError("DXIL conditional execution missing or substituted")
    rows=[dict(case_mask=int(mask),differing_lanes=int(lanes)) for mask,lanes in observations]
    if any(row["case_mask"]&~0x75d or row["differing_lanes"]!=row["case_mask"].bit_count()*4 for row in rows):
        raise ValueError("partial or out-of-domain legacy DXIL differences")
    return dict(available=True,fixed=not legacy,vector_cases=84,checked_words=3656,matched_words=None if legacy else 3656,
                legacy_known_failure=legacy,legacy_observations=rows,warp=True,hardware=hardware,
                scope="native vertex-flag ALU compute execution; raw selection payload, NaN class only; not full rendering")

def read_conditional_dxil_phases(text,hardware):
    adapters=["warp","hardware"] if hardware else ["warp"]
    order=list(CONDITIONAL_DXIL_PHASES)*len(adapters)
    raw=re.findall(r"^DXIL_PHASE (\w+)_ms=(\S+)$",text,re.MULTILINE)
    processes=re.findall(r"^DXIL_PROCESS adapter=(\w+) wall_ms=(\S+)$",text,re.MULTILINE)
    if [name for name,value in raw]!=order or [name for name,value in processes]!=adapters:
        raise ValueError("DXIL phase/process observations missing or duplicated")
    phases=[dict(adapter=adapters[i//len(CONDITIONAL_DXIL_PHASES)],phase=name,ms=float(value)) for i,(name,value) in enumerate(raw)]
    wall=[dict(adapter=name,wall_ms=float(value)) for name,value in processes]
    if any(not math.isfinite(x["ms"]) or x["ms"]<0 for x in phases) or any(not math.isfinite(x["wall_ms"]) or x["wall_ms"]<=0 for x in wall):
        raise ValueError("invalid DXIL phase/process duration")
    if any(sum(x["ms"] for x in phases if x["adapter"]==p["adapter"])>p["wall_ms"] for p in wall):
        raise ValueError("DXIL phase duration exceeds its process envelope")
    return dict(phase_observations=phases,process_observations=wall,
                timing_scope="diagnostic correctness child with debug layer; nonoverlapping root/upload, resource/record, submit/fence wait, readback/check and release envelopes; file input/startup/process exit remain outside; no isolated GPU time or paired timing gate")

def validate_rounding_dxil(source,build,out,results,env,backend,kind="rounding"):
    if kind not in ("rounding","conditional"):raise ValueError("unknown DXIL ALU fixture")
    if not WIN:return dict(available=False,reason="Direct3D12 execution requires Windows")
    legacy=kind=="conditional" and "#define XE_DXIL_VERTEX_CONDITIONAL_FTZ 1" not in (source/"src/xenia/gpu/dxil/guest_alu.h").read_text()
    target=out/(kind+"-dxil");target.mkdir();log=results/(source.name+"-"+kind+"-dxil.log");fixture=env.copy()
    generator=target/"generator.exe";runtime=target/"runtime.exe";module=target/(kind+".dxil")
    command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/wd4201","/D_CRT_SECURE_NO_WARNINGS","/DXE_DXIL_ALU_TEST",
         "/I"+str(source/"src/xenia/gpu/dxil"),"/I"+str(source/"src/xenia/gpu/dxil/bytecode"),
         HERE/"dxil_writer_test.c",HERE/"dxil_alu_test.c","/Fo"+str(target)+os.sep,"/Fe"+str(generator),
         "/link",build/"obj/Windows/xenia-native-dxil.lib","/CGTHREADS:"+str(JOBS)]
    if kind=="conditional":command.insert(1,"/DXE_DXIL_CONDITIONAL_TEST")
    run(command,source,log,fixture,240)
    run([generator,module,"alu"],source,log,fixture,120)
    command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/wd4201","/D_CRT_SECURE_NO_WARNINGS",
         HERE/"dxil_runtime_test.c","/Fo"+str(target/"runtime.obj"),"/Fe"+str(runtime),
         "/link","d3d12.lib","dxgi.lib","dxguid.lib"]
    if legacy:command.insert(1,"/DXE_DXIL_CONDITIONAL_OBSERVATION")
    if kind=="conditional":command.insert(1,"/DXE_DXIL_PHASE_TIMING")
    run(command,source,log,fixture,120)
    expected=Path(str(module)+".expected")
    matched=3656 if kind=="conditional" else 2984
    if not expected.is_file() or expected.stat().st_size!=matched*4:raise ValueError("DXIL ALU expected work missing")
    hardware=backend=="d3d12-hardware"
    for use_hardware in ([False,True] if hardware else [False]):
        fixture["XE_DXIL_TEST_HARDWARE"]="1" if use_hardware else "0"
        started=time.perf_counter_ns();run([runtime,module,expected],source,log,fixture,120)
        if kind=="conditional":
            with log.open("a",encoding="utf-8") as output:
                output.write("DXIL_PROCESS adapter=%s wall_ms=%.6f\n"%("hardware" if use_hardware else "warp",(time.perf_counter_ns()-started)/1e6))
    coverage=validate_conditional_dxil_log(log.read_text(),hardware,legacy) if kind=="conditional" else validate_rounding_dxil_log(log.read_text(),hardware)
    if kind=="conditional":coverage.update(read_conditional_dxil_phases(log.read_text(),hardware))
    coverage["artifacts"]={path.name:dict(bytes=path.stat().st_size,sha256=file_sha256(path)) for path in (module,expected)}
    with zipfile.ZipFile(results/(source.name+"-"+kind+"-dxil.zip"),"w",zipfile.ZIP_DEFLATED) as archive:
        for path in (module,expected):archive.write(path,path.name)
    return coverage

def validate_selection_reference(source,build,out,results,env,backend,kind="selection"):
    if kind not in ("selection","rounding","conditional"):raise ValueError("unknown native ALU fixture")
    rounding=kind=="rounding";conditional=kind=="conditional";cases=84 if conditional else 384 if rounding else 648
    content=(source/"libretro/core_alu_spv.h").read_text()
    if "#define XE_ALU_CONTROL_MINMAX 1" not in content:return dict(available=False,reason="historical source has no MIN/MAX prototype")
    recipe_available="#define XE_ALU_RESUME_ROUNDING 1" in content
    fixed=("#define XE_SPIRV_VERTEX_ROUND_INPUT_FTZ 1" in (source/"libretro/core_float_rtz_emit.h").read_text()
           if rounding else "#define XE_ALU_CONTROL_MINMAX_RAW_SELECTION 1" in content)
    if conditional:fixed="#define XE_ALU_CONDITIONAL_RAW_SELECTION 1" in (source/"libretro/core_alu_operand.h").read_text()
    bridge="shader_rounding_bridge.cc" if conditional else "shader_"+kind+"_bridge.cc"
    target=out/("native-"+kind);target.mkdir();log=results/(source.name+"-"+kind+".log");fixture=env.copy()
    fixture.update(FORWARD_FLOW_SOURCE=str(HERE/("core_shader_"+kind+"_gpu_fixture.c")),
                   FORWARD_FLOW_BRIDGE=str(HERE/bridge),FORWARD_FLOW_OUT=str(target),
                   FORWARD_FLOW_BUILD=str(build),FORWARD_FLOW_LINK_FLAGS="/CGTHREADS:"+str(JOBS))
    if WIN:command=[os.environ.get("COMSPEC","cmd.exe"),"/d","/c",source/"tools/tests/run_forward_flow_tests.cmd"]
    else:
        san=fixture.get("SANITIZER","-fsanitize=address,undefined")
        run([env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",*san.split(),
             "-I"+str(source),"-I"+str(source/"third_party/libretro-common/include"),"-I"+str(HERE),
             "-c",HERE/("core_shader_"+kind+"_gpu_fixture.c"),"-o",target/"driver.o"],source,log,fixture,120)
        fixture.update(CXX=env.get("CXX","c++")+" "+san+" "+str(target/"driver.o")+" -Wl,--strip-debug",
                       TEST_SOURCE=str(HERE/bridge),TEST_BUILD_DIR=str(target),
                       TEST_ARGUMENT=str(target))
        command=["sh",source/"tools/tests/run_shader_publication_tests.sh"]
    failed=False
    try:run(command,source,log,fixture,240)
    except subprocess.CalledProcessError as error:
        if not conditional or fixed or error.returncode!=1:raise
        failed=True
    if conditional and failed!=(not fixed):raise ValueError("conditional legacy diagnostic exit contract missing")
    coverage=(validate_conditional_native_log if conditional else validate_rounding_native_log if rounding else validate_selection_log)(log.read_text(),fixed)
    if conditional:coverage["legacy_generator_exit_observed"]=failed
    if fixed and kind=="selection":
        unit=target/("unit.exe" if WIN else "unit")
        if WIN:command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/I"+str(source),
                        "/I"+str(source/"third_party/libretro-common/include"),HERE/"core_alu_selection_test.c",
                        "/Fo"+str(target/"unit.obj"),"/Fe"+str(unit)]
        else:command=[env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
                      "-I"+str(source),"-I"+str(source/"third_party/libretro-common/include"),HERE/"core_alu_selection_test.c","-o",unit]
        run(command,source,log,fixture,120);run([unit],source,log,fixture,60)
        if log.read_text().splitlines().count("PASS: raw selection anchors,8192 source/modifier checks, host FTZ/DAZ independence")!=1:
            raise ValueError("selection modifier/host-mode unit missing")
        scalar_unit=target/("scalar.exe" if WIN else "scalar")
        scalar_command=[str(arg).replace(str(target/"unit.obj"),str(target/"scalar.obj")).replace(str(unit),str(scalar_unit)) for arg in command]
        scalar_command.insert(1,"/DXE_ALU_SELECTION_FORCE_SCALAR" if WIN else "-DXE_ALU_SELECTION_FORCE_SCALAR")
        run(scalar_command,source,log,fixture,120);run([scalar_unit],source,log,fixture,60)
        if log.read_text().splitlines().count("PASS: raw selection anchors,8192 source/modifier checks, host FTZ/DAZ independence")!=2:
            raise ValueError("selection scalar fallback unit missing")
        coverage.update(unit=True,unit_scalar=True)
    if conditional and fixed:
        marker="PASS: conditional raw selection,84 literal programs,131072 random vectors, double oracle, host-mode and exact-alias controls"
        for scalar in (False,True):
            unit=target/(("scalar" if scalar else "unit")+(".exe" if WIN else ""))
            if WIN:command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/I"+str(source),"/I"+str(source/"third_party/libretro-common/include"),HERE/"core_alu_conditional_test.c","/Fo"+str(unit.with_suffix(".obj")),"/Fe"+str(unit)]
            else:command=[env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2","-I"+str(source),"-I"+str(source/"third_party/libretro-common/include"),HERE/"core_alu_conditional_test.c","-o",unit]
            if scalar:command.insert(1,"/DXE_ALU_SELECTION_FORCE_SCALAR" if WIN else "-DXE_ALU_SELECTION_FORCE_SCALAR")
            run(command,source,log,fixture,120);run([unit],source,log,fixture,60)
            if log.read_text().splitlines().count(marker)!=(2 if scalar else 1):raise ValueError("conditional scalar/SIMD unit missing")
        coverage.update(unit=True,unit_scalar=True)
    if rounding:
        units=[("core_shader_rounding_input_test.c","PASS: rounding input dataflow and malformed/unflushed/wrong-mask/zero-sign controls")]
        if recipe_available:units.append(("core_float_round_emit_test.c","PASS: integer rounding anchors and 131072 random inputs; libm oracle and host-mode independence"))
        for index,(name,marker) in enumerate(units):
            unit=target/(("input-unit" if index==0 else "recipe-unit")+(".exe" if WIN else ""))
            if WIN:command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/I"+str(source),HERE/name,"/Fo"+str(unit.with_suffix(".obj")),"/Fe"+str(unit)]
            else:command=[env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2","-I"+str(source),HERE/name,"-lm","-o",unit]
            run(command,source,log,fixture,120);run([unit],source,log,fixture,60)
            if log.read_text().splitlines().count(marker)!=1:raise ValueError("rounding guard/recipe unit missing")
        coverage.update(unit=True,input_unit=True,recipe_available=recipe_available,recipe_unit=recipe_available)
    if backend.startswith("vulkan"):
        runtime=target/("runtime.exe" if WIN else "runtime")
        if WIN:command=["cl","/nologo","/TC","/MD","/O2","/W4","/WX","/D_CRT_SECURE_NO_WARNINGS",
                        "/DXE_LOOP_ACCURACY_GPU_TEST","/DXE_SATURATION_GPU_TEST","/DXE_SELECTION_FLOAT_CONTROLS","/DXE_VERTEX_TEST_CASES="+str(cases),
                        "/I"+str(source/"third_party/Vulkan-Headers/include"),HERE/"core_shader_call_vulkan_test.c",
                        "/Fo"+str(target/"runtime.obj"),"/Fe"+str(runtime)]
        else:command=[env.get("CC","cc"),"-std=c89","-pedantic","-Wno-long-long","-Wall","-Wextra","-Werror","-O2",
                      "-DXE_LOOP_ACCURACY_GPU_TEST","-DXE_SATURATION_GPU_TEST","-DXE_SELECTION_FLOAT_CONTROLS",
                      "-DXE_LOOP_ADVANCE_VALIDATION","-DXE_VERTEX_TEST_CASES="+str(cases),"-isystem",source/"third_party/Vulkan-Headers/include",
                      HERE/"core_shader_call_vulkan_test.c","-ldl","-o",runtime]
        if conditional:command.insert(1,"/DXE_VERTEX_TEST_INPUT_BYTES=64" if WIN else "-DXE_VERTEX_TEST_INPUT_BYTES=64")
        if rounding and not fixed:command.insert(1,"/DXE_ROUNDING_LEGACY_OBSERVATION" if WIN else "-DXE_ROUNDING_LEGACY_OBSERVATION")
        run(command,source,log,fixture,120);run([runtime,target],source,log,fixture,240)
        text=log.read_text()
        if re.search(r"VUID-|Validation Error|ERROR:",text):raise ValueError("invalid native GPU selection execution")
        if text.splitlines().count("LOOP_VERTEX_CAPTURE vertices=3 shared_views=4")!=1:raise ValueError("selection capture incomplete")
        if not WIN and text.splitlines().count("LOOP_ADVANCE_VALIDATION enabled=1")!=1:raise ValueError("selection validation layer missing")
        marker=(rf"^OBSERVED: {cases} actual guest vertex shaders and feedback guards; known denormal differences retained \(.+\)$" if rounding and not fixed else
                rf"^PASS: {cases} actual guest vertex shaders, exact4-word output and feedback guards \(.+\)$")
        if len(re.findall(marker,text,re.MULTILINE))!=1:
            raise ValueError("native GPU selection work missing")
        if rounding and not fixed:coverage.update(validate_rounding_legacy_log(text))
        if len(re.findall(r"^SELECTION_FLOAT_CONTROLS FTZ_requested=0 FTZ_supported=[01] signed_zero_inf_nan_preserve=1 RTZ_supported=[01] RTE_supported=[01]$",text,re.MULTILINE))!=1:
            raise ValueError("selection float-control coverage missing")
        devices=re.findall(r"^BENCH_DEVICE Vulkan (.+)$",text,re.MULTILINE)
        if len(devices)!=1:raise ValueError("selection device missing")
        software=any(n in devices[0].lower() for n in ("llvmpipe","lavapipe","swiftshader"))
        if software!=(backend=="vulkan-software"):raise ValueError("wrong selection software/hardware device")
        coverage.update(gpu=True,device=devices[0],validation=not WIN,vertices=3,signed_zero_inf_nan_preserve=True,FTZ_requested=False)
    validator=fixture.get("SPIRV_VALIDATOR") or shutil.which("spirv-val")
    if backend.startswith("vulkan") and not validator:raise ValueError("selection checks require spirv-val")
    hashes={};validated=set()
    with zipfile.ZipFile(results/(source.name+"-"+kind+".zip"),"w",zipfile.ZIP_DEFLATED) as archive:
        for case in range(cases):
            for suffix in ("raw.spv","spv","input","loops","expected"):
                path=target/("call"+str(case)+"."+suffix)
                if not path.is_file():raise ValueError("missing selection fixture: "+path.name)
                digest=file_sha256(path);hashes[path.name]=digest;archive.write(path,path.name)
                if suffix.endswith("spv") and validator and digest not in validated:
                    run([validator,"--target-env","vulkan1.2",path],source,log,fixture,30);validated.add(digest)
    coverage.update(artifacts=hashes,validated_modules=len(validated))
    return coverage

def compare_conditional_coverage(coverage,vulkan):
    failures=[];candidate=coverage["candidate"]
    for role,row in coverage.items():
        if not row.get("available"):continue
        if not candidate.get("available"):failures.append("conditional native coverage removed")
        if row.get("fixed") and not candidate.get("fixed"):failures.append("conditional raw CPU selection removed")
        expected=dict(cases=84,cpu_lane_disagreements=0 if row.get("fixed") else 88,
                      cpu_literal_equal=bool(row.get("fixed")),legacy_known_failure=not row.get("fixed"),
                      legacy_generator_exit_observed=not row.get("fixed"),unit=bool(row.get("fixed")),unit_scalar=bool(row.get("fixed")))
        if vulkan:
            expected.update(gpu=True,vertices=3,signed_zero_inf_nan_preserve=True,FTZ_requested=False,validated_modules=6)
            if not row.get("device") or row.get("device")!=candidate.get("device"):failures.append(role+": conditional device mismatch")
            if not WIN and not row.get("validation"):failures.append(role+": conditional validation missing")
        artifacts=row.get("artifacts",{})
        names={f"call{case}.{suffix}" for case in range(84) for suffix in ("raw.spv","spv","input","loops","expected")}
        if set(artifacts)!=names or any(not isinstance(x,str) or not re.fullmatch(r"[0-9a-f]{64}",x) for x in artifacts.values()):
            failures.append(role+": conditional artifacts incomplete")
        if any(row.get(k)!=v for k,v in expected.items()):failures.append(role+": conditional native execution incomplete")
        dxil=row.get("dxil",{})
        if WIN and not dxil.get("available"):failures.append(role+": Windows DXIL conditional execution missing")
        if dxil.get("available"):
            fields=dict(vector_cases=84,checked_words=3656,matched_words=3656 if dxil.get("fixed") else None,
                        legacy_known_failure=not dxil.get("fixed"),warp=True)
            if any(dxil.get(k)!=v for k,v in fields.items()):
                failures.append(role+": DXIL conditional execution incomplete")
            observed=dxil.get("legacy_observations",[])
            count=(2 if dxil.get("hardware") else 1) if not dxil.get("fixed") else 0
            if len(observed)!=count or any(type(x.get("case_mask")) is not int or type(x.get("differing_lanes")) is not int or x["case_mask"]<0 or x["case_mask"]&~0x75d or x["differing_lanes"]!=x["case_mask"].bit_count()*4 for x in observed):
                failures.append(role+": legacy DXIL observations incomplete")
            if not candidate.get("dxil",{}).get("available"):failures.append("DXIL conditional coverage removed")
            if dxil.get("hardware") and not candidate.get("dxil",{}).get("hardware"):
                failures.append("DXIL hardware conditional coverage removed")
            if dxil.get("fixed") and not candidate.get("dxil",{}).get("fixed"):failures.append("DXIL conditional input FTZ removed")
            phase_count=len(CONDITIONAL_DXIL_PHASES)*(2 if dxil.get("hardware") else 1)
            if len(dxil.get("phase_observations",[]))!=phase_count or len(dxil.get("process_observations",[]))!=phase_count//len(CONDITIONAL_DXIL_PHASES):
                failures.append(role+": DXIL phase/process observations missing")
    return dict(revisions=coverage,failures=failures,scope="old CPU88 selected-payload disagreements retained explicitly; no claim it was a correct oracle")

def validate_conditional(source,build,out,results,env,backend):
    row=validate_selection_reference(source,build,out,results,env,backend,"conditional")
    dxil_backend="d3d12-hardware" if backend=="vulkan-hardware" else backend
    row["dxil"]=validate_rounding_dxil(source,build,out,results,env,dxil_backend,"conditional")
    return row
