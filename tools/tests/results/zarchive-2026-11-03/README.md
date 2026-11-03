# ZArchive reader: correctness, resident memory and hardware budget

Measured 3 November 2026 on a Ryzen 9 9950X3D exposed through WSL2, Ubuntu GCC
13.3, guest CPU affinity 2, files on DrvFS. Guest affinity does not pin a host
physical core. The environment was not host-isolated. No power or system ASLR
policy was changed.

The baseline is the published `3ca00408` C++ reader. The new reader uses the
production C89 API directly, matching the core's resolved-file-handle calls.
`run_zarchive_benchmark.sh` reproduces the experiment, including compiler flags,
fixtures, shared-library binding, checksums and controls. Each comparison uses
10 alternating process pairs; identical baseline code runs in 5 further pairs.
A parked libretro-common thread keeps libc's mutex implementation in its real
multithreaded mode. Builds and correctness tests finished before timing.

Both shared objects use the same common stream/codec sources and Make-shaped
`-O3 -mavx -finline-functions -funroll-loops -fomit-frame-pointer -fPIC
-fno-semantic-interposition -DNDEBUG` options with `-Bsymbolic`. Fixture and
measurement code is C89; the historical-reader/test-writer interoperability
bridge retains C++ only where those existing interfaces require it. The
`Current` Git field in the environment file records the measurement's parent
commit: the new reader was then an uncommitted working tree. The recorded source
SHA256 values identify the measured working-file bytes. Canonical Git source object IDs added after staging identify the delivered sources across checkout line-ending normalization.

## Median regression checks

Times cover an entire process workload, not one call. Cached requests copy
256 bytes; warm raw data uses two blocks and warm compressed data uses 32.
RLE cache misses cycle through 8,320 blocks, exceeding the 64-block cache.
The RLE fixture has 520 MiB logical data but about 110 KiB physical data.

| Workload | Iterations | Baseline ms | C89 ms | Change | Identical-baseline control change | Peak RSS KiB, baseline / C89 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Name lookup + raw cache read | 30,000,000 | 497.452 | 400.943 | -19.40% | +0.16% | 7948 / 3820 |
| Name lookup + compressed cache read | 30,000,000 | 512.385 | 517.869 | +1.07% | +0.72% | 8202 / 5904 |
| Resolved-handle raw read | 30,000,000 | 393.158 | 278.412 | -29.19% | -0.08% | 7962 / 3832 |
| Resolved-handle compressed cache read | 30,000,000 | 401.930 | 400.607 | -0.33% | -0.56% | 8204 / 5920 |
| Name lookup + compressed cache miss | 1,200,000 | 629.164 | 616.043 | -2.09% | -0.68% | 8196 / 7958 |
| Resolved-handle compressed cache miss | 1,200,000 | 605.370 | 590.396 | -2.47% | -0.15% | 8204 / 7952 |
| Archive open, read and close, raw | 300 | 490.584 | 399.800 | -18.51% | +0.05% | 8166 / 3828 |
| Archive open, read and close, RLE | 300 | 415.306 | 402.337 | -3.12% | +0.09% | 8180 / 4104 |

The small compressed-cache differences do not establish a general throughput
improvement; name-based cached reads were slightly slower in this sample.
Resolved-handle reads are the core's hot file-read route. Peak RSS is the whole
component process, including loader, codec and worker, not whole-emulator RSS
or unique physical memory. Unwritten cache capacity is not resident memory.
Tests separately verify zero owned metadata bytes and no decoded-cache payload
allocation for native mapped raw reads. The 4 MiB cache remains bounded and is
allocated/touched on demand for decoded or fallback blocks.

The raw warm baseline already copies only the requested bytes after warming.
Its measured gain is not a claim of eliminating a 64 KiB copy on every cache
hit. The new path removes shared cache/lock handling there; raw cold reads also
avoid the old intermediary block fill. These are CPU/cache-hot component
measurements. They do not measure cold storage, NVMe throughput, frontend
latency, game performance or FPS.

## Hardware/data-movement reference

A separate shared-object function performs only `memcpy`, through the same
opaque function-call boundary and access pattern. Assembly confirms a real copy
of all requested bytes; the compiler cannot reduce it to the checked endpoints.
The 256-byte payload requires at least 256 logical input bytes and 256 logical
output bytes. Metadata, validation, publication and synchronization add work.

Ten process samples per reference gave:

| Access pattern | Median ns/call | Best observed ns/call | Median logical read+write GiB/s | Best observed logical GiB/s |
| --- | ---: | ---: | ---: | ---: |
| Two-block raw working set | 4.853 | 2.401 | 98.26 | 198.56 |
| 32-block compressed-cache working set, already decoded | 4.832 | 3.061 | 98.69 | 155.80 |

These are measured feasible copy rates including ABI/bookkeeping overhead,
not a proof of the CPU's theoretical peak or a universal physical lower bound.
The raw resolved-handle reader costs 9.280 ns/call: about 1.91 times the median
copy reference, or 3.86 times the best observed reference. The compressed cache
reader costs 13.354 ns/call: about 2.76 times its median reference, or 4.36 times
its best observed reference. There is substantial remaining headroom; beating
the old implementation is not the optimization ceiling.

The remaining gap includes required extent checks and format translation, plus
raw-run probing or shared cache lookup/LRU/locking. A compressed miss currently
materializes and validates a full private 64 KiB block before copying requested
bytes; that design's traffic must not be confused with the payload-only floor.
Further reductions need workload profiling and ownership/error-publication
proof, rather than skipping validation or exposing failed decompressions.
Cold-open results also need an OS/filesystem latency model; cache-bandwidth
numbers cannot explain DrvFS open latency.

An early rewrite regressed hot compressed reads. Disassembly showed the raw
probe and cold allocation/decode work inlined into the hot read function. The
final implementation separates those paths, uses an inline raw-length tag
check, and reduces the new read function's stack reservation from 120 to 24
bytes in this compiler. This is an assembly-guided change, not an assertion
that source-language choice alone improves throughput. Raw immutable spans
are validated/coalesced and copied without a decoder/cache mutex.

## Correctness and limits

Strict MSVC and MinGW C89 tests, Linux ASan/UBSan and TSan, and Android ARM64/
x86-64 syntax checks cover the new code. TSan on this WSL host required
per-process `setarch x86_64 -R` after a pre-main sanitizer mapping failure;
production flags and global ASLR were unchanged. Tests exercise independent
raw/RLE byte oracles, four concurrent readers, hash collisions and eviction,
malformed ranges/names/directory graphs, failed reads and private checksum
failure publication. Real-writer fixtures cover legacy compatibility, mixed
raw/decoded cache entries, long names and empty files. Full Windows/Linux core
builds, audio cadence/format/retry tests and Windows Vulkan/D3D12 legacy/v2
close/unload regressions passed. No installed RetroArch core was overwritten.

This does not finish global VFS migration, recursive device-mount redesign,
full-game lifetime testing, all memory-safety work or the wider optimization
goal. The pre-existing full-archive footer hash remains unchecked; frame
checksums are enforced when present. Immutable sources and quiescence before
reader destruction remain explicit lifetime requirements.