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
    def sample(self,values,stage="vulkan.vs_ps_pso"):
        return {(stage,0):values}

    def test_target_wait_is_not_a_queue_drain(self):
        calls="cache->AwaitPipelineCompletion(); cache->AwaitPipelineCompletion(pipeline);"
        self.assertEqual(ci.source_wait_alarms(calls, "")["queue_drain_references"], 1)
        self.assertEqual(ci.source_wait_alarms(calls.replace("pipeline);", ");"), "")
                         ["queue_drain_references"], 2)

    def test_device_identity_counts_all_timed_profiles(self):
        identity="BENCH_DEVICE Vulkan vendor=4318 device=11141 driver=1 name=test"
        for count in [1,2,3,4,5,6]:
            self.assertEqual(ci.vulkan_device_identity((identity+"\n")*count,count),identity)

    def test_device_identity_rejects_missing_or_duplicate_profile(self):
        identity="BENCH_DEVICE Vulkan vendor=4318 device=11141 driver=1 name=test\n"
        for expected in [3,4,5,6]:
            for actual in range(8):
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
        self.assertEqual(len(ci.expected_keys("vulkan-hardware")),576)
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
