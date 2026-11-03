# Archive SHA-256 component measurements

Windows 11, AMD Ryzen 9 9950X3D, native MSVC x64 19.44 toolchain,
`/TC /MD /O2`, 3 November 2026. Driver: `tools/tests/core_sha256_benchmark.c`.
Baseline: `5690de9e:third_party/zarchive/src/sha_256.{c,h}`. Current is the
owned libretro-common implementation with the local changes recorded in
`third_party/libretro-common/SOURCE.txt`. Scalar uses the same current source
with `cpu_features_get` renamed to a test probe returning zero.

Seven process pairs for each chunk size, reversing old/new/scalar order on
alternating pairs. Each process hashes 268435456 bytes of identical repeated
input; initialization and finalization are timed, allocation/filling are not.
The printed digest keeps the result observable and matches across all variants.
`clock()` on this Windows CRT measures elapsed host time; this is a test only,
not guest timing. Processes were not pinned; other build work was running.
Raw observations are in `samples.csv`. No isolated-copy or theoretical-peak
measurement is claimed by this hashing test.

| Update chunk | Old median s | Current SHA-NI s | Current forced scalar s |
|---|---:|---:|---:|
| 64 B | 0.513 | 0.137 | 0.506 |
| 64 KiB | 0.508 | 0.130 | 0.500 |
| 16 MiB | 0.511 | 0.130 | 0.499 |

At 64 KiB, the hardware path processes about 1.92 GiB/s versus 0.49 GiB/s
for the old scalar implementation, about 3.91 times the throughput. The
forced fallback remains near the baseline; the small difference is within
the measured spread (old 0.505-0.510 s, scalar 0.494-0.523 s). These are
component results, not game FPS, archive mount time or whole-core RSS.
The writer is not on the reader's hot path.

The irreducible work is SHA-256 compression over every 64-byte block and one
32-byte final digest. Current full blocks go directly to the compressor;
only partial blocks need staging. Runtime dispatch occurs once per input
batch. The remaining hardware-path cost includes sequential hash-state
feedback, message schedule intrinsics, per-block calls, loop/control and the
CPU feature probe per batch. This measurement does not establish the hardware
ceiling or eliminate that remaining gap.

Reproduction: extract the baseline sources with `git show` into a separate
include directory. Compile baseline source and driver with `/DXE_SHA_BASELINE`,
that directory and the owned common include directory. Compile current
`hash/lrc_hash.c` and driver with the common include directory; link the
existing MSVC `retro-common.lib` and `winmm.lib` for file/CPU helper symbols.
Use `/TC /MD /O2` for all objects and no LTO. Run each executable as
`benchmark.exe <chunk-bytes> 268435456`, alternating process order. For scalar,
compile the hash source with `/Dcpu_features_get=xe_test_cpu_features_get` and
the driver with `/DXE_SHA_TEST_SCALAR`.

Correctness is checked separately using SHA-256 empty/abc/56-byte/million-a
known answers, all split points, unaligned data and padding boundaries,
SHA-224 and raw block semantics, plus SHA-1 single/two-message paths. Both
MSVC dispatch and forced fallback pass; Linux ASan/UBSan also passes.
Real writer fixtures verify the footer hash with its digest field zeroed,
then exercise compressed/raw archive reads through native and frontend VFS.
`source-sha256.txt` hashes canonical Git/LF source bytes. Measurement inputs
differ only in platform line endings, header indentation, and a comment-only
correction describing the Windows clock as elapsed host time.
