# Production fiber comparison

Windows x64, Ryzen 9 9950X3D, MSVC 19.44.35224.0. The old guarded-stack Fiber
implementation comes from commit `967d10598afc0a7b2f9d33f770efd528457756f9`;
the new implementation uses owned libco and the C89 context adapter. Both
executables link the same C89 driver, C++ bridge and dependency libraries.
The historical assembly is extracted only for this developer comparison.
The production core has no Boost.Context dependency.

Reproduce from an x64 Native Tools prompt after building the MSVC core:

```bat
tools\tests\build_core_fiber_benchmark.cmd
powershell -ExecutionPolicy Bypass -File tools\tests\run_core_fiber_benchmark.ps1
```

`FIBER_TEST_LIBDIR` can select the MSVC core's library directory. The default
is `build/cmake-msvc-native/obj/Windows`. Git history must contain the baseline
commit. The runner accepts repository-relative `BinaryDirectory` and
`OutputDirectory` paths. It alternates backend order over twelve rounds,
pins the measuring thread to affinity mask 4, and uses 1 MiB guarded stacks.

The 240 raw samples are in `results.csv`; `summary.csv` contains medians and
ranges. `environment.json` and `binaries.csv` identify the environment and
measured executables. No native compiler processes were observed for this run.
Switch cases warm 100,000 round trips and time 2,000,000, excluding allocation.
Reported switch nanoseconds divide round-trip time by two.

| Sequential fibers | Boost ns/switch | libco ns/switch |
| --- | ---: | ---: |
| 1 | 10.91 | 4.87 |
| 16 | 11.95 | 5.10 |
| 128 | 12.25 | 5.48 |
| 512 | 15.36 | 6.75 |

Random-order switching also improves across 16, 128 and 512 fibers. This is
about 2.2-2.3 times the switching throughput in this harness, not game speed.
Creation is not uniformly faster: warmed creation has overlapping ranges,
and first-child creation medians are 16.80 us for Boost and 16.90 us for libco.
At 512 sequential fibers, the median increase over each process's pre-create
sample is 2,182 versus 2,374 KiB of peak working set and 36,052 versus 36,224 KiB
of private commit. Separate libco context records cost a little more memory;
this change does not establish a memory reduction or superiority in every
category. Timings include host scheduling and allocator variability.

Correctness coverage includes C89 external-stack reset tests, register/FP
context fixtures, production-wrapper callback return and suspended restart,
native-thread readoption, migration after host-thread exit, eight concurrent
workers and deep-stack growth. Linux ASan/UBSan runs pass with stack-use-after-
return detection enabled and disabled; enabled runs check 3,069 retired
fake-stack mappings. Full MSVC, MinGW and Linux core builds pass; Windows
cadence/audio/content-close/APC and Vulkan/D3D12 post-unload regressions pass,
as do Linux cadence/audio smoke tests. ARM64 context tests run under QEMU;
native ARM64, Android and macOS full builds were not run for this change.
These checks do not establish title compatibility, gameplay performance,
PMU cache-miss counts, or the cause of historical content-close crashes.
