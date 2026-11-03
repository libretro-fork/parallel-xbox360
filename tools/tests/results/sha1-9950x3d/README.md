# SHA-1 component measurements

Machine: AMD Ryzen 9 9950X3D, WSL Linux x86-64, GCC 13, CPU 2 pinned.
Ten separate process samples per variant/mode/size alternate forward/reverse mode
order. All use `-O3 -mavx -fPIC`, separate translation units and no LTO. Normal
runtime dispatch uses SHA instructions; the scalar variant redirects only the
hash translation unit's feature probe to a zero-returning test function.

Modes 0/1 are old TinySHA1 and common one-shot hashes. Modes 2/3 model the old and
new guest init/update/final adapters, including byte-state conversion/publication.
The old algorithm and class bridge come from the historical licensed header at
`d40cc603`; no old implementation is retained in the production source tree.
Every process verifies its final digest and emits a consumed checksum. One
unaligned, cache-hot input is reused. The 512-byte and larger cases process
128 MiB each; smaller cases use 262144 iterations. Reported values below are
medians. Raw process times, host-mode results, checksums, compiler/CPU information
and source hashes are stored alongside this report.
variant | bytes | Tiny guest MiB/s | common guest MiB/s | ratio
--- | ---: | ---: | ---: | ---:
dispatch | 1048576 | 301.26 | 2740.74 | 9.098
dispatch | 20 | 79.19 | 436.13 | 5.507
dispatch | 3 | 12.10 | 66.46 | 5.493
dispatch | 4096 | 296.48 | 2653.83 | 8.951
dispatch | 512 | 263.87 | 2128.28 | 8.065
dispatch | 64 | 133.07 | 800.38 | 6.015
dispatch | 65536 | 298.61 | 2734.80 | 9.158
scalar | 1048576 | 302.38 | 329.19 | 1.089
scalar | 20 | 79.10 | 88.13 | 1.114
scalar | 3 | 12.02 | 13.29 | 1.106
scalar | 4096 | 297.51 | 325.64 | 1.095
scalar | 512 | 263.89 | 288.08 | 1.092
scalar | 64 | 133.18 | 143.08 | 1.074
scalar | 65536 | 298.86 | 330.87 | 1.107

The hardware-dispatched guest model is about 5.5-9.2 times faster in these cases;
forced scalar is about 1.07-1.11 times faster. This measures these component
workloads on this machine, not gameplay, cold-cache streaming, other CPUs, or the
full guest kernel-dispatch overhead. The two binaries' unchanged Tiny guest
baseline medians differ by less than 1%, a useful noise check. This is not a
measurement of the hardware's theoretical ceiling.

Complete SHA blocks are read directly by common compression. Each SHA-1 block
requires 80 algorithm rounds; padding compresses one or two more blocks and
exports a 20-byte digest. The guest API additionally requires state-word and
observable tail/padding writes. Eliminating those mandatory bytes would change
the API. The new adapter avoids whole-context import on short partial updates,
imports only the valid tail, publishes only the updated tail after Update and
retains the full final padded block as required. Runtime SHA dispatch and direct block processing reduce the old byte-loop
overhead. Both current scalar SHA-1 implementations retain an 80-word schedule;
reducing that storage remains a separate optimization to measure. Instruction
throughput/dependency limits and cache-cold memory bandwidth still require their
own roofline measurements before claiming a hardware limit has been reached.

Reproduce serially on an idle Linux machine (default runs all seven sizes):

```
BENCH_CPU=2 BENCH_PAIRS=10 bash tools/tests/run_sha1_benchmark.sh build/tests/sha1-benchmark
```

These measurements were collected in two serial runs with `BENCH_SIZES` equal to
`512 4096 65536 1048576` and `3 20 64`. No project builds ran during timing. Host
profiling clocks exist only in the benchmark executable.
