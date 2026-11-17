"""Reject missing native work, substituted goldens and malformed timing evidence."""
import unittest
from unittest.mock import patch
import shader_conditional_ci as ci

class ConditionalTests(unittest.TestCase):
    def test_hardware_run_requires_both_native_backends(self):
        for backend,expected in (("vulkan-hardware","d3d12-hardware"),("d3d12-hardware","d3d12-hardware"),
                                 ("vulkan-software","vulkan-software"),("d3d12-warp","d3d12-warp")):
            with patch.object(ci,"validate_selection_reference",return_value={}) as native, \
                 patch.object(ci,"validate_rounding_dxil",return_value={}) as dxil:
                ci.validate_conditional(None,None,None,None,{},backend)
            self.assertEqual(native.call_args.args[-2:],(backend,"conditional"))
            self.assertEqual(dxil.call_args.args[-2:],(expected,"conditional"))

    def test_hardware_coverage_cannot_disappear(self):
        old=dict(available=True,dxil=dict(available=True,hardware=True))
        new=dict(available=True,dxil=dict(available=True,hardware=False))
        result=ci.compare_conditional_coverage(dict(base=old,candidate=new),False)
        self.assertIn("DXIL hardware conditional coverage removed",result["failures"])

    def test_dxil_phase_observations_are_complete_finite_and_inside_process(self):
        text="".join("DXIL_PHASE "+name+"_ms=1.000000\n" for name in ci.CONDITIONAL_DXIL_PHASES)+"DXIL_PROCESS adapter=warp wall_ms=10.000000\n"
        self.assertEqual(len(ci.read_conditional_dxil_phases(text,False)["phase_observations"]),9)
        for changed in ("",text+text,text.replace("pso_ms=1","pso_ms=nan"),text.replace("device_ms=1","device_ms=-1"),text.replace("wall_ms=10","wall_ms=2"),text.replace("adapter=warp","adapter=hardware")):
            with self.assertRaises(ValueError):ci.read_conditional_dxil_phases(changed,False)
        with self.assertRaises(ValueError):ci.read_conditional_dxil_phases(text,True)
        for name in ci.CONDITIONAL_DXIL_PHASES:
            line="DXIL_PHASE "+name+"_ms=1.000000\n"
            for changed in (text.replace(line,""),text.replace(line,line+line),text.replace(line,line.replace("1.000000","inf"))):
                with self.subTest(phase=name):
                    with self.assertRaises(ValueError):ci.read_conditional_dxil_phases(changed,False)
        swapped=text.replace("root_upload_ms","temporary_ms").replace("pso_ms","root_upload_ms").replace("temporary_ms","pso_ms")
        with self.assertRaises(ValueError):ci.read_conditional_dxil_phases(swapped,False)
        hardware=text.replace("adapter=warp","adapter=hardware")
        self.assertEqual(len(ci.read_conditional_dxil_phases(text+hardware,True)["phase_observations"]),18)

    def test_dxil_conditional_requires_raw_selection_and_complete_execution(self):
        text="DXIL_CONDITIONAL_CASES vector=84 raw_payload=1 FTZ_condition_expected=1\nDXIL_ROUNDING_CASES scalar=96 vector=96 NaN_class_only=1 FTZ_expected=1\nNative DXIL execution: 3656 words matched.\nD3D12 WARP pipeline and execution: 0x00000000\n"
        self.assertTrue(ci.validate_conditional_dxil_log(text)["warp"])
        for changed in ("",text+text,text.replace("vector=84","vector=83"),text.replace("3656","3655"),text.replace("raw_payload=1","raw_payload=0"),text.replace("0x00000000","0x80004005")):
            with self.assertRaises(ValueError):ci.validate_conditional_dxil_log(changed)
        with self.assertRaises(ValueError):ci.validate_conditional_dxil_log(text,True)
        hardware=text+"Native DXIL execution: 3656 words matched.\nD3D12 hardware pipeline and execution: 0x00000000\n"
        self.assertTrue(ci.validate_conditional_dxil_log(hardware,True)["hardware"])
        legacy=text.replace("Native DXIL execution: 3656 words matched.","OBSERVED: native DXIL 3656 words checked; conditional differences retained")
        legacy+="DXIL_CONDITIONAL_LEGACY_OBSERVATION case_mask=1885 differing_lanes=32; not a correct historical reference\n"
        observed=ci.validate_conditional_dxil_log(legacy,False,True)
        self.assertIsNone(observed["matched_words"])
        self.assertEqual(observed["checked_words"],3656)
        with self.assertRaises(ValueError):ci.validate_conditional_dxil_log(legacy)
        with self.assertRaises(ValueError):ci.validate_conditional_dxil_log(text,False,True)
        for before,after in (("case_mask=1885","case_mask=2"),("differing_lanes=32","differing_lanes=31")):
            with self.assertRaises(ValueError):ci.validate_conditional_dxil_log(legacy.replace(before,after),False,True)

    def test_conditional_native_audit_keeps_legacy_quantization_failure(self):
        for fixed,differences in ((False,88),(True,0)):
            text=f"CONDITIONAL_AUDIT programs=84 CPU_lane_differences={differences}; raw payload literals, last12 probe condition FTZ\n"
            row=ci.validate_conditional_native_log(text,fixed)
            self.assertEqual(row["cpu_literal_equal"],fixed)
            for changed in ("",text+text,text.replace("84","83"),text.replace(f"differences={differences}","differences=87")):
                with self.assertRaises(ValueError):ci.validate_conditional_native_log(changed,fixed)

    def test_conditional_native_coverage_requires_work_units_device_and_artifacts(self):
        artifacts={f"call{case}.{suffix}":"0"*64 for case in range(84) for suffix in ("raw.spv","spv","input","loops","expected")}
        old=dict(available=True,fixed=False,cases=84,cpu_lane_disagreements=88,cpu_literal_equal=False,
                 legacy_known_failure=True,legacy_generator_exit_observed=True,unit=False,unit_scalar=False,
                 gpu=True,vertices=3,signed_zero_inf_nan_preserve=True,FTZ_requested=False,
                 validated_modules=6,device="same GPU",validation=True,artifacts=artifacts,
                 dxil=dict(available=True,fixed=False,vector_cases=84,checked_words=3656,matched_words=None,warp=True,
                           legacy_known_failure=True,legacy_observations=[dict(case_mask=1885,differing_lanes=32)],
                           phase_observations=[dict(adapter="warp",phase=p,ms=1.0) for p in ci.CONDITIONAL_DXIL_PHASES],
                           process_observations=[dict(adapter="warp",wall_ms=10.0)]))
        new=dict(old,fixed=True,cpu_lane_disagreements=0,cpu_literal_equal=True,
                 legacy_known_failure=False,legacy_generator_exit_observed=False,unit=True,unit_scalar=True,
                 dxil=dict(old["dxil"],fixed=True,matched_words=3656,legacy_known_failure=False,legacy_observations=[]))
        self.assertFalse(ci.compare_conditional_coverage(dict(base=old,candidate=new),True)["failures"])
        self.assertTrue(ci.compare_conditional_coverage(dict(base=new,candidate=old),True)["failures"])
        for field,value in (("available",False),("cases",83),("cpu_lane_disagreements",88),("cpu_literal_equal",False),
                            ("legacy_generator_exit_observed",True),("unit",False),("unit_scalar",False),
                            ("gpu",False),("vertices",1),("validated_modules",5),("device","other GPU"),
                            ("artifacts",{}),("artifacts",dict(artifacts,wrong="0"*64)),
                            ("dxil",dict(available=False)),("dxil",dict(old["dxil"],matched_words=3655))):
            with self.subTest(field=field):
                self.assertTrue(ci.compare_conditional_coverage(dict(base=old,candidate=dict(new,**{field:value})),True)["failures"])

if __name__=="__main__":unittest.main()
