"""Guard the guardrail: reject corrupt/missing data and masked regressions."""
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

spec=importlib.util.spec_from_file_location("shader_compile_ci",Path(__file__).with_name("shader_compile_ci.py"))
ci=importlib.util.module_from_spec(spec);spec.loader.exec_module(ci)


class ComparisonTests(unittest.TestCase):
    def test_gpu_submission_order_and_positive_negative_controls_are_required(self):
        meta="GPU_TIMESTAMP queue=0 bits=64 period_ns=1 repetitions=1 record_delay=0 ordering=semaphore_chain3\n"
        executed="GPU_TIMESTAMP_EXECUTED repetitions=1 dispatches=3 record_delay=0\n"
        self.assertEqual(ci.gpu_timestamp_log_metadata(meta+executed,1)["bits"],64)
        for log in ("",meta,executed,meta+meta+executed,meta+executed.replace("dispatches=3","dispatches=2"),
                    meta.replace("semaphore_chain3","unordered")+executed,meta.replace("bits=64","bits=0")+executed):
            with self.assertRaises(ValueError):ci.gpu_timestamp_log_metadata(log,1)
        with self.assertRaises(ValueError):ci.gpu_timestamp_log_metadata(meta+executed+meta.replace("queue=0","queue=1")+executed,2)
        def data(stage,value):return {("raw_control."+stage,i):[value] for i in range(3)}
        single=data("gpu_dispatch",.001);repeat=data("gpu_dispatch_repeat8",.008);delay=data("gpu_dispatch",.001)
        self.assertEqual(ci.validate_gpu_calibration(single,repeat,delay,"raw_control")["work_ratio"],8)
        for a,b,c in ((data("gpu_dispatch",.000001),repeat,delay),(single,data("gpu_dispatch_repeat8",.001),delay),
                      (single,repeat,data("gpu_dispatch",.025)),(data("gpu_dispatch",0),repeat,delay)):
            with self.assertRaises(ValueError):ci.validate_gpu_calibration(a,b,c,"raw_control")
        missing=dict(single);missing.pop(("raw_control.gpu_dispatch",0))
        with self.assertRaises(ValueError):ci.validate_gpu_calibration(missing,repeat,delay,"raw_control")

    def test_gpu_dispatch_gate_preserves_first_use_and_anchor(self):
        data={("raw_control.gpu_dispatch",i):[1e-6]*7 for i in range(3)}
        candidate={k:list(v) for k,v in data.items()};candidate[("raw_control.gpu_dispatch",0)]=[1]*7
        result=ci.compare_gpu_dispatch(dict(base=data,candidate=candidate),7,"raw_control")
        self.assertFalse(result["failure"]);self.assertEqual(result["first_use_observed"]["candidate"]["maximum"],1)
        self.assertIn("UNVERIFIED",result["first_use_cache_status"])
        candidate[("raw_control.gpu_dispatch",1)]=[.02]*7
        result=ci.compare_gpu_dispatch(dict(base=data,candidate=candidate,anchor=data),7,"raw_control")
        self.assertTrue(result["failure"]);self.assertTrue(any(x["failures"] for x in result["anchor_cases"]))
        self.assertTrue(ci.compare_gpu_dispatch(dict(base=data),7,"raw_control")["failure"])
        missing=dict(data);missing.pop(("raw_control.gpu_dispatch",2))
        with self.assertRaisesRegex(ValueError,"GPU dispatch case/sample"):
            ci.compare_gpu_dispatch(dict(base=data,candidate=missing),7,"raw_control")
        with self.assertRaises(ValueError):ci.compare_gpu_dispatch(dict(candidate=data),6,"raw_control")

    def test_gpu_tick_availability_width_period_and_conversion_are_verified(self):
        raw={("raw_control.gpu_dispatch",i):[250e-9]*7 for i in range(3)}
        rows=[["raw_control.gpu_dispatch",str(i),"64","c8","1","1","64","2.5",".001"] for _ in range(7) for i in range(3)]
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"ticks.csv"
            def write(values):path.write_text("".join(",".join(x)+"\n" for x in values))
            write(rows);self.assertEqual(ci.validate_gpu_ticks(path,raw,7,"raw_control")["bits"],64)
            for changes in ({4:"0"},{5:"0"},{6:"0"},{6:"35"},{6:"65"},{7:"nan"},{7:"-1"},
                            {8:"nan"},{8:"-1"},{8:".000000001"},{6:"36",8:"100"},
                            {6:"36",2:"1000000000"},{0:"raw_program.gpu_dispatch"},{1:"7"}):
                bad=[list(x) for x in rows]
                for at,value in changes.items():bad[0][at]=value
                write(bad)
                with self.assertRaises(ValueError):ci.validate_gpu_ticks(path,raw,7,"raw_control")
            for bad in (rows[:-1],rows+[rows[0]],[rows[0]+["extra"]]+rows[1:]):
                write(bad)
                with self.assertRaises(ValueError):ci.validate_gpu_ticks(path,raw,7,"raw_control")
            write(rows);wrong={k:list(v) for k,v in raw.items()};wrong[("raw_control.gpu_dispatch",1)][0]=1
            with self.assertRaisesRegex(ValueError,"conversion mismatch"):ci.validate_gpu_ticks(path,wrong,7,"raw_control")

    def test_gpu_tick_wrapping_and_ordinary_profile_identity(self):
        raw={("raw_control.gpu_dispatch",i):[8e-9]*5 for i in range(3)}
        start=(1<<36)-5
        rows=[["raw_control.gpu_dispatch",str(i),format(start,"x"),"3","1","1","36","1",".001"] for _ in range(5) for i in range(3)]
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/"ticks.csv";path.write_text("".join(",".join(x)+"\n" for x in rows))
            self.assertTrue(all(x["wrapped"] for x in ci.validate_gpu_ticks(path,raw,5,"raw_control")["samples"]))
        log="PROFILE_VALID_WORK active=315 inactive=428;\nPROFILE_VALID_WORK_GPU active=315 inactive=428;\n"
        ci.validate_valid_workload(log,{"raw_control"});ci.validate_valid_workload(log,{"raw_control"},gpu=True)
        with self.assertRaises(ValueError):ci.validate_valid_workload(log.replace("PROFILE_VALID_WORK_GPU","OTHER"),{"raw_control"},gpu=True)

    def test_snapshot_native_and_owner_coverage_are_required(self):
        old=dict(native=True,cases=96,owner_available=True,owner_unit=True)
        self.assertFalse(ci.compare_ucode_coverage(dict(base=old,candidate=old))["failures"])
        for field,value in [("native",False),("cases",0),("owner_available",False),("owner_unit",False)]:
            candidate=dict(old);candidate[field]=value
            self.assertTrue(ci.compare_ucode_coverage(dict(base=old,candidate=candidate))["failures"])
        self.assertTrue(ci.compare_ucode_coverage(dict(base=old))["failures"])

    def test_snapshot_batches_reject_missing_or_substituted_cases(self):
        data={("shader.ucode.snapshot_batch2000",i):[.001]*7 for i in range(24)}
        self.assertFalse(ci.compare_ucode_snapshot(dict(base=data,candidate=data),7)["failure"])
        self.assertTrue(ci.compare_ucode_snapshot(dict(base=data),7)["failure"])
        missing=dict(data);missing.pop(("shader.ucode.snapshot_batch2000",23))
        with self.assertRaisesRegex(ValueError,"missing snapshot"):
            ci.compare_ucode_snapshot(dict(base=data,candidate=missing),7)
        short=dict(data);short[("shader.ucode.snapshot_batch2000",0)]=[.001]*6
        with self.assertRaisesRegex(ValueError,"missing snapshot"):
            ci.compare_ucode_snapshot(dict(base=data,candidate=short),7)

    def test_valid_workload_profile_cannot_be_missing_or_substituted(self):
        program="PROFILE_VALID_WORK active=363 inactive=380; inactive lanes\n"
        control="PROFILE_VALID_WORK active=315 inactive=428; inactive lanes\n"
        ci.validate_valid_workload(program+control,{"raw_program","raw_control"})
        ci.validate_valid_workload(control,{"raw_control"})
        ci.validate_valid_workload("",set())
        for log in ("",program,program+program,program+control+control,
                    program+control.replace("315","314")):
            with self.assertRaisesRegex(ValueError,"missing or substituted"):
                ci.validate_valid_workload(log,{"raw_program","raw_control"})

    def test_control_feature_coverage_cannot_be_removed_or_skipped(self):
        old=dict(available=True,unit=True,vulkan_gpu=True,boolean_cf_supported=True,
                 nonempty_loops_supported=True,predicate_cf_supported=True,
                 comparison_alu_supported=True,comparison_context_cases=1536,
                 lazy_source_supported=True,lazy_source_context_cases=64,register_borrow_supported=True,register_context_cases=896,minmax_supported=True,minmax_context_cases=5136,
                 boolean_context_cases=224,loop_context_cases=32,predicate_context_cases=1254)
        for field,value in [("available",False),("unit",False),("vulkan_gpu",False),
                            ("boolean_cf_supported",False),("nonempty_loops_supported",False),
                            ("predicate_cf_supported",False),("predicate_context_cases",0),
                            ("comparison_alu_supported",False),("comparison_context_cases",0),
                            ("lazy_source_supported",False),("lazy_source_context_cases",0),
                            ("register_borrow_supported",False),("register_context_cases",0),
                            ("minmax_supported",False),("minmax_context_cases",0),
                            ("boolean_context_cases",0),("loop_context_cases",0)]:
            candidate=dict(old);candidate[field]=value
            self.assertTrue(ci.compare_control_coverage(dict(base=old,candidate=candidate),True)["failures"],field)
        self.assertFalse(ci.compare_control_coverage(dict(base=dict(available=False),candidate=old),True)["failures"])
        self.assertFalse(ci.compare_control_coverage(dict(candidate=dict(available=True,unit=True,vulkan_gpu=False)),False)["failures"])

    def test_full_context_correctness_cannot_inherit_timing_only_mode(self):
        for backend in ("vulkan-software","d3d12-warp"):
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run") as run:
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True);(root/"libretro").mkdir()
                (root/"libretro/core_alu_spv.h").write_text("xe_alu_control_spv_build(\n#define XE_ALU_CONTROL_BOOL 1\n#define XE_ALU_CONTROL_LOOPS 1\n#define XE_ALU_CONTROL_PREDICATE 1\n")
                (tests/("run_alu_program_tests.cmd" if ci.WIN else "run_alu_program_tests.sh")).touch()
                result=ci.validate_raw_program(root,root,root,root/"log",{"XE_ALU_PROGRAM_TIMING_ONLY":"1","XE_ALU_PROGRAM_VALID_WORK":"1"},backend,control=True)
                self.assertNotIn("XE_ALU_PROGRAM_TIMING_ONLY",run.call_args.args[3])
                self.assertNotIn("XE_ALU_PROGRAM_VALID_WORK",run.call_args.args[3])
                self.assertEqual(result["control_flow_version"],4)
                self.assertTrue(result["predicate_cf_supported"])
                self.assertEqual(result["predicate_context_cases"],1254 if backend.startswith("vulkan") else 0)
                self.assertEqual(result["boolean_context_cases"],224 if backend.startswith("vulkan") else 0)
                self.assertEqual(result["loop_context_cases"],36 if backend.startswith("vulkan") else 0)

    def test_comparison_context_support_is_explicit_and_not_a_warp_execution_claim(self):
        for backend in ("vulkan-software","d3d12-warp"):
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run"):
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True);(root/"libretro").mkdir()
                (root/"libretro/core_alu_spv.h").write_text("xe_alu_control_spv_build(\n#define XE_ALU_CONTROL_COMPARE 1\n")
                (tests/("run_alu_program_tests.cmd" if ci.WIN else "run_alu_program_tests.sh")).touch()
                (root/"log").write_text("COMPARE_CONTEXT replays=1536 invocations=3072\n")
                result=ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)
                self.assertEqual(result["control_flow_version"],5)
                self.assertTrue(result["comparison_alu_supported"])
                self.assertEqual(result["comparison_context_cases"],1536 if backend.startswith("vulkan") else 0)

    def test_comparison_context_execution_cannot_be_missing_or_substituted(self):
        marker="COMPARE_CONTEXT replays=1536 invocations=3072\n"
        ci.validate_comparison_workload(marker)
        for log in ("",marker.replace("1536","0"),marker.replace("3072","2"),marker+marker):
            with self.assertRaisesRegex(ValueError,"comparison context execution"):
                ci.validate_comparison_workload(log)

    def test_lazy_source_context_execution_is_required_only_for_vulkan(self):
        marker="LAZY_SOURCE_CONTEXT replays=64 invocations=128\n"
        ci.validate_lazy_source_workload(marker)
        for log in ("",marker.replace("64","0"),marker.replace("128","2"),marker+marker):
            with self.assertRaisesRegex(ValueError,"lazy-source context execution"):
                ci.validate_lazy_source_workload(log)
        for backend in ("vulkan-software","d3d12-warp"):
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run"):
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True);(root/"libretro").mkdir()
                (root/"libretro/core_alu_spv.h").write_text("xe_alu_control_spv_build(\n#define XE_ALU_CONTROL_LAZY_SOURCE 1\n")
                (tests/("run_alu_program_tests.cmd" if ci.WIN else "run_alu_program_tests.sh")).touch()
                (root/"log").write_text(marker)
                result=ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)
                self.assertEqual(result["control_flow_version"],6)
                self.assertTrue(result["lazy_source_supported"])
                self.assertEqual(result["lazy_source_context_cases"],64 if backend.startswith("vulkan") else 0)

    def test_borrowed_register_context_execution_is_required(self):
        marker="REGISTER_CONTEXT replays=896 invocations=1792\n"
        ci.validate_register_workload(marker)
        for text in ("",marker+marker,marker.replace("896","0"),marker.replace("1792","2")):
            with self.assertRaisesRegex(ValueError,"register ownership"):
                ci.validate_register_workload(text)
        for backend in ("vulkan-software","d3d12-warp"):
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run"):
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True);(root/"libretro").mkdir()
                (root/"libretro/core_alu_spv.h").write_text("xe_alu_control_spv_build(\n#define XE_ALU_CONTROL_BORROW_REGISTERS 1\n")
                (tests/("run_alu_program_tests.cmd" if ci.WIN else "run_alu_program_tests.sh")).touch()
                (root/"log").write_text(marker)
                value=ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)
                self.assertTrue(value["register_borrow_supported"])
                self.assertEqual(value["register_context_cases"],896 if backend.startswith("vulkan") else 0)
                (root/"log").write_text("")
                if backend.startswith("vulkan"):
                    with self.assertRaisesRegex(ValueError,"register ownership"):
                        ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)

    def test_minmax_and_cold_storage_execution_guards(self):
        marker="MINMAX_CONTEXT replays=5136 invocations=10272\n"
        storage="CONTROL_COLD_STORAGE allocations=1 capacity_words=16384\n"
        ci.validate_minmax_workload(marker);ci.validate_control_cold_storage(storage)
        for text in ("",marker+marker,marker.replace("5136","0"),marker.replace("10272","2")):
            with self.assertRaisesRegex(ValueError,"minmax context"):
                ci.validate_minmax_workload(text)
        for text in ("",storage+storage,storage.replace("=1 ","=2 "),storage.replace("16384","8192")):
            with self.assertRaisesRegex(ValueError,"cold control allocation"):
                ci.validate_control_cold_storage(text)
        for backend in ("vulkan-software","d3d12-warp"):
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run"):
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True);(root/"libretro").mkdir()
                (root/"libretro/core_alu_spv.h").write_text("xe_alu_control_spv_build(\n#define XE_ALU_CONTROL_MINMAX 1\n")
                (tests/("run_alu_program_tests.cmd" if ci.WIN else "run_alu_program_tests.sh")).touch()
                (root/"log").write_text(marker+storage)
                value=ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)
                self.assertTrue(value["minmax_supported"])
                self.assertEqual(value["minmax_context_cases"],5136 if backend.startswith("vulkan") else 0)
                self.assertEqual(value["cold_control_allocations"],1)
                (root/"log").write_text(storage)
                if backend.startswith("vulkan"):
                    with self.assertRaisesRegex(ValueError,"minmax context"):
                        ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)
                (root/"log").write_text(marker)
                with self.assertRaisesRegex(ValueError,"cold control allocation"):
                    ci.validate_raw_program(root,root,root,root/"log",{},backend,control=True)

    def test_control_program_historical_absence_is_explicit(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run") as run:
            root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True)
            (tests/("run_alu_program_tests.cmd" if ci.WIN else "run_alu_program_tests.sh")).touch()
            result=ci.validate_raw_program(root,root,root,root/"log",{},"vulkan-software",control=True)
            self.assertFalse(result["available"]);self.assertFalse(result["unit"])
            run.assert_not_called()

    def test_control_program_modes_and_pinned_inputs(self):
        for windows,backend,gpu in [(False,"vulkan-software",True),(True,"d3d12-warp",False)]:
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"WIN",windows),patch.object(ci,"run") as run:
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True);(root/"libretro").mkdir()
                (root/"libretro/core_alu_spv.h").write_text("xe_alu_control_spv_build(")
                (tests/("run_alu_program_tests.cmd" if windows else "run_alu_program_tests.sh")).touch()
                result=ci.validate_raw_program(root,root/"build",root/"out",root/"log",{},backend,control=True)
                fixture=run.call_args.args[3]
                self.assertTrue(result["unit"]);self.assertEqual(result["vulkan_gpu"],gpu)
                self.assertEqual(fixture["ALU_CONTROL_TEST"],"1")
                self.assertEqual(fixture["ALU_PROGRAM_OUT"],str(root/"out/raw-control"))
                self.assertEqual(result["cf_step_limit"],4096)
                self.assertEqual(result["call_depth_limit"],4)
                self.assertEqual(result["pinned_control_fixture_sha256"],ci.file_sha256(ci.HERE/"shader_control_program_fixture.h"))
                self.assertFalse(result["renderer_integration"])

    def test_control_program_uses_separate_timing_channel_and_gates(self):
        raw={k:(v[:]) for k,v in self.raw_program_samples().items()}
        control={(stage.replace("raw_program.","raw_control."),case):values for (stage,case),values in raw.items()}
        intro=ci.compare_raw_program({"candidate":control},7,"raw_control.")
        self.assertTrue(intro["new_component_without_parent"])
        self.assertFalse(intro["comparable"])
        self.assertTrue(ci.compare_raw_program({"base":control},7,"raw_control.")["missing_candidate"])
        worse={key:values[:] for key,values in control.items()};worse[("raw_control.compute_pso_warm",0)]=[.1]*7
        report=ci.compare_raw_program({"base":control,"candidate":worse,"anchor":control},7,"raw_control.")
        self.assertTrue(report["failure"]);self.assertTrue(report["anchor_cases"])
        with self.assertRaises(ValueError):ci.compare_raw_program({"candidate":control},7)

    def test_loop_reference_historical_absence_is_explicit(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(ci,"run") as run:
            root=Path(folder)
            result=ci.validate_loop_reference(root,root,root,root/"log",{},"vulkan-software")
            self.assertFalse(result["available"])
            self.assertFalse(result["native"])
            self.assertFalse(result["vulkan_gpu"])
            run.assert_not_called()

    def test_loop_reference_backend_and_pinned_inputs(self):
        for windows,backend,gpu in [(False,"vulkan-software",True),(True,"d3d12-warp",False),(True,"vulkan-hardware",True)]:
            with tempfile.TemporaryDirectory() as folder,patch.object(ci,"WIN",windows),patch.object(ci,"run") as run:
                root=Path(folder);(root/"libretro").mkdir()
                (root/"libretro/core_shader_loop.h").write_text("xe_shader_loop_advance(")
                result=ci.validate_loop_reference(root,root/"build",root/"out",root/"log",{},backend)
                self.assertTrue(result["unit"]);self.assertTrue(result["native"])
                self.assertEqual(result["vulkan_gpu"],gpu)
                fixture_calls=[call for call in run.call_args_list if len(call.args)>3 and
                               "FORWARD_FLOW_SOURCE" in call.args[3]]
                self.assertEqual(len(fixture_calls),1)
                fixture=fixture_calls[0].args[3]
                self.assertEqual(fixture["FORWARD_FLOW_SOURCE"],str(ci.HERE/"core_shader_loop_gpu_fixture.c"))
                self.assertEqual(fixture["FORWARD_FLOW_BRIDGE"],str(ci.HERE/"shader_loop_accuracy_bridge.cc"))
                if windows:self.assertEqual(fixture["FORWARD_FLOW_BUILD"],str(root/"build"))

    def test_loop_reference_execution_failure_is_not_a_pass(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(ci,"run",side_effect=subprocess.CalledProcessError(1,"unit")):
            root=Path(folder);(root/"libretro").mkdir()
            (root/"libretro/core_shader_loop.h").write_text("xe_shader_loop_advance(")
            with self.assertRaises(subprocess.CalledProcessError):
                ci.validate_loop_reference(root,root/"build",root/"out",root/"log",{},"vulkan-software")

    def raw_program_samples(self):
        keys={("raw_program."+x,0) for x in ("emit","module","compute_pso","compute_pso_warm")}
        keys.update(("raw_program.submit_complete",i) for i in range(3))
        return {key:[.001]*7 for key in keys}

    def test_new_raw_program_cost_has_no_fabricated_parent(self):
        result=ci.compare_raw_program({"candidate":self.raw_program_samples()},7)
        self.assertFalse(result["comparable"])
        self.assertTrue(result["new_component_without_parent"])
        self.assertEqual(result["cases"],[])
        self.assertEqual(len(result["observed"]["candidate"]),7)

    def test_existing_raw_program_removal_is_a_failure(self):
        result=ci.compare_raw_program({"base":self.raw_program_samples()},7)
        self.assertTrue(result["failure"])
        self.assertTrue(result["missing_candidate"])
        self.assertTrue(ci.compare_raw_program({"anchor":self.raw_program_samples()},7)["missing_candidate"])

    def test_raw_program_regression_and_anchor_are_gated(self):
        baseline=self.raw_program_samples();candidate=self.raw_program_samples()
        candidate[("raw_program.compute_pso_warm",0)]=[.012]*7
        result=ci.compare_raw_program({"base":baseline,"candidate":candidate,"anchor":baseline},7)
        self.assertTrue(result["comparable"])
        self.assertTrue(result["failure"])
        self.assertTrue(any(r["failures"] for r in result["anchor_cases"]))
        self.assertEqual(len(result["observed"]["candidate"][0]["outliers"]),5)

    def test_uncontrolled_first_use_is_retained_and_never_claimed_verified(self):
        baseline=self.raw_program_samples();candidate=self.raw_program_samples()
        candidate[("raw_program.compute_pso",0)]=[.34]*7
        result=ci.compare_raw_program({"base":baseline,"candidate":candidate},7)
        self.assertIn("UNVERIFIED",result["first_use_cache_status"])
        self.assertNotIn("raw_program.compute_pso",{r["stage"] for r in result["cases"]})
        self.assertEqual(next(r["maximum"] for r in result["observed"]["candidate"] if r["stage"]=="raw_program.compute_pso"),.34)

    def test_raw_program_missing_metric_or_samples_is_not_a_pass(self):
        raw=self.raw_program_samples();raw.pop(("raw_program.emit",0))
        with self.assertRaises(ValueError):ci.compare_raw_program({"candidate":raw},7)
        with self.assertRaises(ValueError):ci.compare_raw_program({"candidate":self.raw_program_samples()},6)
        with self.assertRaises(ValueError):ci.compare_raw_program({},3)

    def test_raw_program_historical_absence_is_explicit(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(ci,"run") as run:
            result=ci.validate_raw_program(Path(folder),Path(folder),Path(folder),Path(folder)/"log",{},"vulkan-software")
            self.assertFalse(result["available"])
            self.assertFalse(result["unit"])
            self.assertFalse(result["vulkan_gpu"])
            run.assert_not_called()

    def test_raw_program_backend_coverage_and_build_identity(self):
        for windows,backend,gpu in [(False,"vulkan-software",True),(True,"d3d12-warp",False),(True,"vulkan-hardware",True)]:
            with tempfile.TemporaryDirectory() as folder, patch.object(ci,"WIN",windows),patch.object(ci,"run") as run:
                root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True)
                (tests/("run_alu_program_tests.cmd" if windows else "run_alu_program_tests.sh")).touch()
                env={"preserved":"value"}
                result=ci.validate_raw_program(root,root/"build",root/"out",root/"log",env,backend)
                self.assertTrue(result["unit"]);self.assertEqual(result["vulkan_gpu"],gpu)
                self.assertFalse(result["renderer_integration"])
                self.assertFalse(result["full_guest_shader_coverage"])
                actual=run.call_args.args[3]
                self.assertEqual(actual["GPU_TESTS"],"1" if gpu else "0")
                self.assertEqual(actual["preserved"],"value")
                self.assertEqual(actual["ALU_PROGRAM_OUT"],str(root/"out/raw-program"))
                self.assertEqual(actual["ALU_PROGRAM_FIXTURE"],str(ci.HERE/"core_alu_program_vulkan_test.c"))
                self.assertEqual(actual["ALU_PROGRAM_BRIDGE"],str(ci.HERE/"alu_program_bridge.cc"))
                if windows:self.assertEqual(actual["ALU_PROGRAM_BUILD"],str(root/"build"))
                self.assertEqual(env,{"preserved":"value"})

    def test_raw_program_failure_is_not_masked(self):
        with tempfile.TemporaryDirectory() as folder,patch.object(ci,"WIN",False):
            root=Path(folder);tests=root/"tools/tests";tests.mkdir(parents=True)
            (tests/"run_alu_program_tests.sh").touch()
            with patch.object(ci,"run",side_effect=subprocess.CalledProcessError(1,"fixture")):
                with self.assertRaises(subprocess.CalledProcessError):
                    ci.validate_raw_program(root,root,root,root/"log",{},"vulkan-software")

    def sample(self,values,stage="vulkan.vs_ps_pso"):
        return {(stage,0):values}

    def test_target_wait_is_not_a_queue_drain(self):
        calls="cache->AwaitPipelineCompletion(); cache->AwaitPipelineCompletion(pipeline);"
        self.assertEqual(ci.source_wait_alarms(calls, "")["queue_drain_references"], 1)
        self.assertEqual(ci.source_wait_alarms(calls.replace("pipeline);", ");"), "")
                         ["queue_drain_references"], 2)

    def test_device_identity_counts_all_timed_profiles(self):
        identity="BENCH_DEVICE Vulkan vendor=4318 device=11141 driver=1 name=test"
        for count in [1,2,3,4,5,6,7,8]:
            self.assertEqual(ci.vulkan_device_identity((identity+"\n")*count,count),identity)

    def test_device_identity_rejects_missing_or_duplicate_profile(self):
        identity="BENCH_DEVICE Vulkan vendor=4318 device=11141 driver=1 name=test\n"
        for expected in [3,4,5,6,7,8]:
            for actual in range(10):
                if actual!=expected:
                    with self.assertRaises(ValueError):ci.vulkan_device_identity(identity*actual,expected)
        with self.assertRaises(ValueError):ci.vulkan_device_identity("",0)

    def test_device_identity_rejects_mixed_drivers(self):
        identity="BENCH_DEVICE Vulkan vendor=4318 device=11141 driver=1 name=test\n"
        with self.assertRaises(ValueError):
            ci.vulkan_device_identity(identity*2+identity.replace("driver=1","driver=2"),3)

    def test_device_identity_does_not_accept_command_text(self):
        with self.assertRaises(ValueError):
            ci.vulkan_device_identity("COMMAND ['BENCH_DEVICE Vulkan fake']\n",1)

    def test_cpu_diagnostics_preserve_paired_sample_order(self):
        key=("vulkan.vs_ps_pso",0)
        data=ci.cpu_diagnostics({key:[.3,.1,.2]},{key:[.02,.01,.03]},3)
        self.assertEqual(data[0]["paired_samples"][0],dict(sample=0,wall_seconds=.3,process_cpu_seconds=.02))
        self.assertEqual(data[0]["process_cpu"]["median"],.02)

    def test_cpu_diagnostics_allow_parallel_cpu_above_wall_and_zero_ticks(self):
        key=("vulkan.vs_ps_pso",0)
        data=ci.cpu_diagnostics({key:[.1,.1]},{key:[.3,0]},2)
        self.assertEqual(data[0]["paired_samples"][0]["process_cpu_seconds"],.3)
        self.assertEqual(data[0]["paired_samples"][1]["process_cpu_seconds"],0)

    def test_cpu_diagnostics_reject_missing_or_substituted_data(self):
        key=("vulkan.vs_ps_pso",0)
        for cpu in [{},{("vulkan.module",0):[.01]*7},{key:[.01]*6}]:
            with self.assertRaises(ValueError):ci.cpu_diagnostics({key:[.02]*7},cpu,7)

    def test_flat_cpu_does_not_hide_elapsed_tail_failure(self):
        wall=self.sample([.01]*6+[.2]);cpu=self.sample([.01]*7)
        rows,anchor,diagnostics=ci.compare_timing_channels(
            dict(base=self.sample([.01]*7),candidate=wall),dict(base=cpu,candidate=cpu),7)
        self.assertTrue(rows[0]["failures"])
        self.assertFalse(anchor)
        self.assertIn("candidate",diagnostics)

    def test_timing_channels_reject_missing_cpu_role(self):
        with self.assertRaises(ValueError):
            ci.compare_timing_channels(dict(base=self.sample([.01]*7),candidate=self.sample([.01]*7)),
                                       dict(base=self.sample([.01]*7)),7)

    def test_timing_channels_keep_anchor_elapsed_gate(self):
        sample=self.sample([.01]*7)
        rows,anchor,diagnostics=ci.compare_timing_channels(
            dict(base=sample,candidate=self.sample([.02]*7),anchor=sample),
            dict(base=sample,candidate=sample,anchor=sample),7)
        self.assertTrue(rows[0]["failures"])
        self.assertTrue(anchor[0]["failures"])

    def test_complete_report_keeps_wall_failure_with_flat_cpu(self):
        # Synthetic report integration only: no compiler, driver or child executable.
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/"source";compiled=source/"compiled";compiled.mkdir(parents=True)
            gpu=source/"src/xenia/gpu/vulkan";gpu.mkdir(parents=True)
            for name in ["vulkan_command_processor.cc","vulkan_pipeline_cache.cc"]:(gpu/name).write_text("")
            for name in ["cpu","dxil","warp"]:(compiled/(name+".bin")).write_bytes(b"identity")
            for i in range(64):
                (compiled/("shader%03u.spv"%i)).write_bytes(ci.struct.pack("<5I",0x07230203,0x10000,0,1,0))
            def prepare(source_arg,out,results,backend,reuse):
                (compiled/"gpu-timestamp-math").mkdir(exist_ok=True)
                (compiled/"gpu-timestamp-math/coverage.json").write_text(json.dumps(dict(unit=True,version=1,synthetic_fixture=True)))
                (results/(source.name+"-"+compiled.parent.name+"-"+compiled.name+"-raw-control.json")).write_text(
                    json.dumps(dict(available=False,unit=False,vulkan_gpu=False)))
                for role in ["base","candidate"]:
                    wall=[];cpu=[]
                    for sample in range(7):
                        for stage,case in sorted(ci.expected_keys("d3d12-warp")):
                            elapsed=.2 if role=="candidate" and stage=="d3d12.graphics_pso" and case==0 and sample==6 else .001
                            wall.append("%s,%d,%.9f\n"%(stage,case,elapsed))
                            cpu.append("%s,%d,0.001000000\n"%(stage,case))
                    (results/(role+".csv")).write_text("".join(wall))
                    (results/(role+"-cpu.csv")).write_text("".join(cpu))
                return dict(source=source,out=compiled,**{name:compiled/(name+".bin") for name in ["cpu","dxil","warp"]})
            args=SimpleNamespace(out=str(root/"work"),base="test",head="test",backend="d3d12-warp",
                                 samples=7,self_check=str(source),reuse_build=None)
            with patch.dict(ci.os.environ,{"GITHUB_STEP_SUMMARY":""}), \
                 patch.object(ci,"prepare",side_effect=prepare),patch.object(ci,"run"), \
                 patch.object(ci,"git",return_value="test"), \
                 patch.object(ci.subprocess,"run",return_value=SimpleNamespace(stdout="{}",stderr="",returncode=0)), \
                 patch("builtins.print"):
                self.assertEqual(ci.run_comparison(args),1)
            report=json.loads((root/"work/results/comparison.json").read_text())
            self.assertFalse(report["success"])
            tail=[x for x in report["cases"] if x["stage"]=="d3d12.graphics_pso" and x["case"]==0][0]
            self.assertTrue(tail["failures"])
            self.assertIn("|d3d12.graphics_pso|0|6|",(root/"work/results/summary.md").read_text())

    def test_bad_clock_stops_before_building_core(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);results=root/"results";results.mkdir()
            commands=[]
            with patch.object(ci,"run",side_effect=lambda command,*args,**kwargs:commands.append(command)), \
                 patch.object(ci,"validate_clock_fixture",side_effect=ValueError("bad clock")):
                with self.assertRaisesRegex(ValueError,"bad clock"):
                    ci.prepare(root,root/"out",results,"d3d12-warp")
            self.assertEqual(len(commands),1)
            self.assertTrue(any(str(x).endswith("core_shader_clock_test.c") for x in commands[0]))

    def test_binary_hash_known_vector(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/"binary";p.write_bytes(b"abc")
            self.assertEqual(ci.file_sha256(p),"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")

    def test_binary_hash_includes_suffix_after_stream_chunk(self):
        with tempfile.TemporaryDirectory() as folder:
            a,b=Path(folder)/"a",Path(folder)/"b"
            prefix=b"x"*(1024*1024)
            a.write_bytes(prefix+b"a");b.write_bytes(prefix+b"b")
            self.assertNotEqual(ci.file_sha256(a),ci.file_sha256(b))

    def test_equal_and_faster(self):
        for values in [[.01]*7,[.005]*7]:
            self.assertFalse(ci.compare(self.sample([.01]*7),self.sample(values),7)[0]["failures"])

    def test_median_regression(self):
        self.assertTrue(ci.compare(self.sample([.01]*7),self.sample([.02]*7),7)[0]["failures"])

    def test_single_large_tail_does_not_hide_in_median(self):
        self.assertTrue(ci.compare(self.sample([.01]*7),self.sample([.01]*6+[1]),7)[0]["failures"])

    def test_noise_floor(self):
        self.assertFalse(ci.compare(self.sample([.001]*7),self.sample([.0011]*7),7)[0]["failures"])

    def test_missing_case(self):
        with self.assertRaises(ValueError):ci.compare(self.sample([.01]*7),{},7)

    def test_missing_sample(self):
        with self.assertRaises(ValueError):ci.compare(self.sample([.01]*7),self.sample([.01]*6),7)

    def test_too_few_samples(self):
        with self.assertRaises(ValueError):ci.compare(self.sample([.01]*3),self.sample([.01]*3),3)

    def test_percentiles_and_max_are_separate(self):
        row=ci.compare(self.sample([.01]*6+[.012]),self.sample([.01]*7),7)[0]
        self.assertLess(row["base"]["median"],row["base"]["p95"])
        self.assertLess(row["base"]["p99"],row["base"]["maximum"])

    def test_invalid_raw_measurements(self):
        for text in ["", "x,0,nan\n", "x,0,inf\n", "x,-1,1\n", "x,0,-1\n", "x,0,1,extra\n"]:
            with tempfile.TemporaryDirectory() as folder:
                p=Path(folder)/"raw.csv";p.write_text(text)
                with self.assertRaises(ValueError):ci.read_samples(p)

    def test_valid_raw_measurements(self):
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder)/"raw.csv";p.write_text("spirv.translate_batch4,3,0.125\n")
            self.assertEqual(ci.read_samples(p),{("spirv.translate_batch4",3):[.125]})

    def test_expected_cases_are_identified_not_just_counted(self):
        self.assertEqual(len(ci.expected_keys("vulkan-software")),96)
        self.assertEqual(len(ci.expected_keys("d3d12-warp")),80)
        self.assertEqual(len(ci.expected_keys("vulkan-hardware")),768)
        self.assertIn(("spirv.softdot_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.softdot_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.softdot_module",15),ci.expected_keys("vulkan-hardware"))
        self.assertNotIn(("vulkan.softdot_module",16),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("spirv.partialmad_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.partialmad_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.partialmad_module",15),ci.expected_keys("vulkan-hardware"))
        self.assertNotIn(("vulkan.partialmad_module",16),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("spirv.softfrac_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.softfrac_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.softfrac_module",15),ci.expected_keys("vulkan-hardware"))
        self.assertNotIn(("vulkan.softfrac_module",16),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("spirv.mad_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.mad_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.mad_module",15),ci.expected_keys("vulkan-hardware"))
        self.assertNotIn(("vulkan.mad_module",16),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("spirv.frac_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.frac_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.frac_module",15),ci.expected_keys("vulkan-hardware"))
        self.assertNotIn(("vulkan.frac_module",16),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("spirv.dot_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.dot_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("spirv.rtz_translate_batch4",63),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.rtz_vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertIn(("vulkan.vs_ps_pso",15),ci.expected_keys("vulkan-hardware"))
        self.assertNotIn(("vulkan.vs_ps_pso",16),ci.expected_keys("vulkan-hardware"))

    def test_multi_commit_push_includes_each_parent(self):
        with tempfile.TemporaryDirectory() as folder:
            def git(*args,**unused):
                return subprocess.check_output(["git",*args],cwd=folder,text=True).strip()
            git("init","-q")
            git("config","user.name","CI test")
            git("config","user.email","ci@example.invalid")
            commits=[]
            for i in range(4):
                (Path(folder)/"file").write_text(str(i));git("add","file");git("commit","-qm",str(i))
                commits.append(git("rev-parse","HEAD"))
            with patch.object(ci,"git",git):
                selected=ci.select_commits("push",commits[3],commits[0])
            self.assertEqual([x["sha"] for x in selected],commits[1:])
            self.assertEqual([x["base"] for x in selected],commits[:3])
            with patch.object(ci,"git",git):
                branch=ci.select_commits("push",commits[3],"0"*40,commits[1:])
            self.assertEqual(branch,selected)
            git("checkout","-qb","side",commits[0])
            (Path(folder)/"side").write_text("side");git("add","side");git("commit","-qm","side")
            side=git("rev-parse","HEAD")
            git("checkout","-q","--detach",commits[3]);git("merge","--no-ff","-qm","merge",side)
            merge=git("rev-parse","HEAD")
            with patch.object(ci,"git",git):
                selected=ci.select_commits("push",merge,commits[0])
            self.assertEqual({x["sha"] for x in selected},set(commits[1:]+[side,merge]))
            for row in selected:self.assertEqual(row["base"],git("rev-parse",row["sha"]+"^1"))


if __name__=="__main__":unittest.main()
