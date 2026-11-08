"""Guard the guardrail: reject corrupt/missing data and masked regressions."""
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

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
        self.assertEqual(len(ci.expected_keys("vulkan-hardware")),192)
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
