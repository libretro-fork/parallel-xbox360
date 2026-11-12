"""Run untimed numeric-bank MSAA images and independent fault controls.

Integer aliases belong to the readback oracle only. These checks do not measure
driver compilation, GPU throughput or cold-play smoothness.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("backend", choices=("vulkan", "d3d12"))
    parser.add_argument("executable", type=Path)
    parser.add_argument("prefix", type=Path)
    parser.add_argument("logs", type=Path)
    parser.add_argument("--typed", action="store_true", help="Production typed images with untimed oracle copies")
    args = parser.parse_args()
    flags = ["IDENTITY_MSAA_BAD_" + name for name in ("SAMPLE", "SCALE", "MASK")]
    env = os.environ.copy()
    for flag in flags + ["MSAA_DYNAMIC_RENDERING", "MSAA_DISABLE_SAMPLE_SHADING", "IDENTITY_MSAA_TYPED_RESOURCES"]:
        env.pop(flag, None)
    if args.typed:
        env["IDENTITY_MSAA_TYPED_RESOURCES"] = "1"
    args.logs.mkdir(parents=True, exist_ok=True)

    def run(name, settings, expected_header, failure=None):
        current = env.copy()
        current.update(settings)
        command = [str(args.executable.resolve()), str(args.prefix)]
        log = args.logs / (name + ".log")
        with log.open("x", encoding="utf-8") as output:
            result = subprocess.run(command, env=current, stdout=output,
                                    stderr=subprocess.STDOUT, timeout=180)
        data = log.read_text(encoding="utf-8")
        if re.search(r"validation error|VUID-", data, re.IGNORECASE):
            raise RuntimeError(f"Vulkan API validation reported a defect: {log}")
        if expected_header not in data:
            raise RuntimeError(f"Missing/contaminated control flags: {log}")
        if failure is None:
            if result.returncode or "PASS:" not in data:
                raise RuntimeError(f"Image verification failed: {log}")
            if args.backend == "vulkan":
                device = re.search(r"native2=([01]) sampleShading=([01]) dynamicRendering=([01])", data)
                count = re.search(r"PASS: (\d+) numeric MSAA identity transfers", data)
                if not device or not count:
                    raise RuntimeError(f"Missing Vulkan coverage metadata: {log}")
                native, shading, dynamic = map(int, device.groups())
                typed = re.search(r"typedResources=([01])", data)
                if not typed or int(typed[1]) != int(args.typed):
                    raise RuntimeError(f"Wrong Vulkan image ownership/format mode: {log}")
                if dynamic != int("MSAA_DYNAMIC_RENDERING" in settings):
                    raise RuntimeError(f"Wrong rendering mode: {log}")
                if "MSAA_DISABLE_SAMPLE_SHADING" in settings and shading:
                    raise RuntimeError(f"Manual sample mode not exercised: {log}")
                if int(count[1]) != (native + 1) * 4 * 3 * 6 * (6 if shading else 4):
                    raise RuntimeError(f"Incomplete Vulkan case count: {log}")
            else:
                device = re.search(r"IDENTITY_MSAA_D3D12 hardware([01]) native2=([01])", data)
                count = re.search(r"PASS: (?:WARP|hardware D3D12) (\d+) numeric production-bank", data)
                if not device or not count:
                    raise RuntimeError(f"Missing D3D12 coverage metadata: {log}")
                hardware, native = map(int, device.groups())
                typed = re.search(r"typedResources=([01])", data)
                if not typed or int(typed[1]) != int(args.typed):
                    raise RuntimeError(f"Wrong D3D12 image ownership/format mode: {log}")
                if hardware != int("MSAA_HARDWARE" in env):
                    raise RuntimeError(f"Wrong D3D12 adapter class: {log}")
                if int(count[1]) != (native + 1) * 216:
                    raise RuntimeError(f"Incomplete D3D12 case count: {log}")
        elif result.returncode != 1 or failure not in data or "PASS:" in data:
            raise RuntimeError(f"Fault did not fail at its intended image: {log}")
        print(f"Verified {name}: {log}", flush=True)

    clean = "IDENTITY_MSAA_CONTROL sample0 scale0 mask0"
    run("render-pass", {}, clean)
    if args.backend == "vulkan":
        run("manual", {"MSAA_DISABLE_SAMPLE_SHADING": "1"}, clean)
        run("dynamic", {"MSAA_DYNAMIC_RENDERING": "1"}, clean)
        run("dynamic-manual", {"MSAA_DYNAMIC_RENDERING": "1",
                               "MSAA_DISABLE_SAMPLE_SHADING": "1"}, clean)
        sites = ["sm0 dm2 native0 scale0 mode1 pixel0,0 sample0",
                 "sm0 dm0 native0 scale1 mode0 pixel1,0 sample0",
                 "sm0 dm1 native0 scale0 mode1 pixel0,0 sample0"]
    else:
        sites = ["sm1 dm0 native0 scale0 mode0 pixel2,0 sample0",
                 "sm0 dm0 native0 scale1 mode0 pixel1,0 sample0",
                 "sm0 dm1 native0 scale0 mode0 pixel0,0 sample1"]
    for index, (flag, site) in enumerate(zip(flags, sites)):
        header = "IDENTITY_MSAA_CONTROL sample%d scale%d mask%d" % tuple(
            int(index == column) for column in range(3))
        run(flag.lower(), {flag: "1"}, header, site)


if __name__ == "__main__":
    main()
