# Core runtime and raw shader checks


## Raw SPIR-V operand storage

`run_spv_operands_tests.sh` exercises the production C89 storage with allocation
failure, poisoned buffers, inline/growing words and flags, overflow and reuse.
Its C++ bridge checks the actual Instruction API against independent word and
string oracles. Linux defaults to ASan/UBSan; use `SANITIZER=` for MinGW.

`run_spv_module_compare.sh` builds the baseline and current raw binary builder,
including post-processing, and compares five complete compute modules byte for
byte. `BASELINE_COMMIT` selects the control; `SANITIZER` enables instrumentation
and `SPIRV_VALIDATOR` optionally validates each output. The tested outputs also
pass `spirv-val --target-env vulkan1.1`.

`run_spv_operands_bench.sh` separates tracked allocation counts from plain CRT
construction/serialization timing. Finish other builds before measuring;
`BUILD_ONLY=1` prepares executables without timing. Results, machine/compiler
settings and limits are in `spv_operands_bench_results.txt`. These are synthetic
storage timings, not whole-shader compilation or gameplay measurements.

`run_file_span_tests.sh` checks C89 mapped ranges against an independent wide-integer oracle, destination guards, overflow, overlap and logical-tail clamping. Its C++ bridge exercises the production `MappedMemory::Slice` method and verifies that valid views borrow the exact backing bytes. Linux defaults to ASan/UBSan; MinGW uses `SANITIZER=`.

`run_file_blocks_tests.sh` exercises the production C89 extent owner and reader: byte-oracle reads across gaps/overlap/fragment changes, whole-request preflight before writes, direct destination pointers, short/error/oversized callback responses, overflow, missing coverage, allocation failure and ownership cleanup. The 64-adjacent-extent control retains one metadata record and makes one backing read; transfer-time allocator counts stay unchanged. This is a component call/storage check, not a retail disk-throughput or RSS benchmark.

## Owned libretro-common primitives and codecs

`core_audio_registry_test.c` checks a reader deliberately paused with a lease,
nonblocking retirement, a slot bound that includes undrained retirement, exact
multi-stream mixing, 4,000 concurrent stream lifetimes and root-owner release
with eight full-queue writers. It also replaces a subsystem group during a
deliberately held frontend lease and cancels one full group while another keeps
playing. Compile the queue with `XE_AUDIO_REGISTRY_TEST`
to enable its test-only lease checkpoint, and test both mirrored and forced
plain rings. The checkpoint is absent from production builds. A host-monotonic
deadline bounds fixture synchronization; it never drives emulated timing.
MSVC/MinGW C89 and Linux ASan/UBSan cover both backends. Linux ThreadSanitizer
also covers the plain backend (`-no-pie` is needed by the local TSan runtime).

`core_input_analog_test.c` exhausts all 65,536 input values for raw sticks,
saturating Y inversion, trigger monotonicity and nearest-quantization error.
It also recovers all 256 physical trigger levels after truncated or rounded
frontend expansion. `core_input_driver_test.c` uses the minimal C++ bridge in
`core_input_driver_bridge.cc` to exercise the actual `LibretroInputDriver`:
frontend polling, guest-state marshalling, small movements, endpoints, trigger
levels and digital fallback. Compile its driver and bridge as C++20 and the
test/`core_input_analog.c`, `core_input_mailbox.c` and `core_audio_cadence.c` as C89. It needs no emulator, GPU or game.

`core_run_cadence_test.c` and `core_audio_format_test.c` use the native loader
in `core_test_module.h` on Windows or POSIX (`-ldl`). Both run against the
compiled core; the format fixture accepts hardware setup without resetting it.

`core_audio_queue_test.c` checks 200,000 frames each of stereo and 5.1 concurrent
production/consumption, exact additive sample order, whole-frame publication,
physical wraps, destination bounds, the 64 ms limit, readiness wakeup and stop
while full. Build it with `core_audio_queue.c`, `core_audio_convert.c` and the common SPSC, eventcount,
thread, feature and time sources. Test both normal and `XE_AUDIO_QUEUE_PLAIN`
builds to force the non-mirrored wrap path. These are C89 component tests;
failed-content core lifecycle tests do not substitute for a loaded-game audio
or media-player teardown test.

`core_audio_samples_test.c` checks final float normalization and legacy integer
conversion, including nonfinite inputs and destination bounds. The test exhausts
all 65,536 int16 round trips and checks low-amplitude nearest-even ties,
adjacent float values and saturation boundaries. Compile it with
`core_audio_samples.c` as C89. `core_audio_format_test.c` exercises the real
native core ABI with eight frontend-format modes, 20 runs each: stereo float,
float-only and integer-only 5.1, float preference, unsupported/empty callbacks,
legacy fallback and callback retirement at unload. It accepts hardware setup
without resetting the context, so it checks silent cadence and negotiation
without a GPU or guest title, not game-produced audio correctness. Arguments:
core library, private system directory and a synthetic invalid XEX file.
Append 50 to repeat all eight modes at the maximum supported batch of 960
audio frames per run, including six-channel float and integer output.

`core_ui_queue_test.c` checks headless button policy, frontend-driven close
timing, active-request lifetime, the 64-request undrained bound, 8,000 completions
from four producer threads, and teardown with an unfinished request. Compile
with `core_ui_queue.c`, `core_audio_cadence.c` and the selected common sources
below. Strict MSVC C89, MinGW C89 and Linux ASan/UBSan runs pass. These tests
exercise queue ownership and policy, not game-specific UI compatibility.

`core_image_test.c`, linked with `libretro/core_image.c`, checks shared codec
linkage, PNG metadata, exact RGBA encode/decode and truncated input rejection.
The guest image codecs remain usable after removal of the standalone UI.

`core_hash_test.c` checks legacy XXH3/XXH64 golden folds over three seeds,
unaligned input and lengths spanning short-input, stripe and block boundaries
through roughly 1 MiB. Segmented streaming and intermediate digest calls must
match complete-input hashes. The folds were generated with the former xxHash
implementation, rather than comparing only two paths in the replacement.

`core_decompress_test.c` checks independent raw/zlib/gzip fixtures, empty output,
exact output sizes, capped growing output, history across reallocations,
truncation and corrupt trailers. Generated streams cover compression levels
0, 3, 6 and 9. Both tests compile as MSVC C89.

`libretro_common_test.c` covers native atomic publication, 100,000 SPSC/eventcount
handoffs, MPSC nodes, hash vectors and a DEFLATE round trip. Define
`RETRO_ATOMIC_FORCE_MSVC` or `RETRO_ATOMIC_FORCE_GCC_NEW` consistently with the
compiler, plus `HAVE_THREADS` and `HAVE_THREAD_STORAGE`, and link the selected
library sources listed in `third_party/CMakeLists.txt` (Windows also needs winmm).
`core_rzstd_test.c` checks a reference-generated level-9 multiblock/checksum
fixture, 180 encoded cases across sizes and patterns, output bounds, truncation
workspace reuse after errors, and exact-boundary terminal matches with offsets 1 through 32. `core_zarchive_test.c` uses a minimal C++
bridge to exercise the real archive classes: compressed/raw blocks, cross-block
random reads, four-MiB cache eviction and empty files. Its test driver is C89.
Hash timings and limits are
recorded in `optimization_measurements.md`.
These tests do not establish that the core's remaining C++ threading has been
migrated, or measure gameplay performance.

## Frame and audio cadence

`core_audio_cadence_test.c` checks 800,000 consecutive fractional-clock steps at
50, 60, 60000/1001, 59.94, 120, 144, 165 and 240 Hz, plus reset and invalid-rate
handling. It links `libretro/core_audio_cadence.c` and compiles as MSVC C89.
`core_run_cadence_test.c` loads the core without a game or GPU and verifies one
video callback and 800 silent stereo frames on every run, then switches to the
per-sample callback and verifies the same sample duration. The older core fails
because a missing mixer suppresses the audio callback entirely.

```bat
cl /TC /W4 /WX /O2 /Ilibretro libretro\core_audio_cadence.c tools\tests\core_audio_cadence_test.c
core_audio_cadence_test.exe
cl /TC /W4 /WX /O2 /Ilibretro tools\tests\core_run_cadence_test.c
core_run_cadence_test.exe C:\absolute\parallel_xbox360_libretro.dll
```

## Windows content-close regression

`core_content_close_test.c` is a C89 libretro frontend that starts the real
emulator with synthetic invalid content, checks that a logging writer was
created, closes content, and requires that writer to be joined. It repeats
startup/teardown three times in one DLL, checks for any surviving threads whose
entry points lie in the core image, then verifies that the DLL really unloads. It needs a working
Vulkan driver, but no game or ROM. Use a private system directory because core
setup creates logs, embedded data and a test profile there.

From a Visual Studio developer prompt at the repository root:

```bat
cl /TC /W4 /WX /O2 /Ilibretro tools\tests\core_content_close_test.c
mkdir build\content-close-system
echo synthetic invalid content>build\invalid.xex
core_content_close_test.exe C:\absolute\parallel_xbox360_libretro.dll C:\absolute\build\content-close-system C:\absolute\build\invalid.xex
```

This exercises failed-load cleanup and DLL thread lifetime, not gameplay or
frontend-shared-device rendering.

`core_vulkan_lifetime_test.c` accepts Vulkan negotiation, rejects a retained
core device-destroy callback, and creates actual negotiated devices. One device
survives context destruction and a second survives content close and verified
core DLL unload; the frontend waits for and destroys each device afterwards.
This directly covers a video thread destroying its cached device after unload.

`core_d3d12_lifetime_test.c` opens synthetic invalid content using the D3D12
backend, exercises repeated context reset/destruction, and checks that no device
negotiation callbacks were registered. After verified core DLL unload it creates
a fence on the frontend's device and signals it from the frontend's queue,
requiring completion within five seconds before releasing frontend COM objects.
An optional fourth argument selects negotiation coverage: Vulkan modes 0 through
4 cover no support query, explicit v1, v2 wrapper creation, rejected v2 and
wrapper failure. D3D12 modes 0 through 3 cover legacy, v2, rejected negotiation
and an older returned render interface. Run each mode separately.
These are lifecycle checks without guest frames; they do not test frame-copy
synchronization or gameplay.

Build in a Visual Studio developer prompt, then run either executable with the
same three absolute-path arguments as `core_content_close_test.exe`:

```bat
cl /TC /W4 /WX /O2 /Ilibretro /Ithird_party\Vulkan-Headers\include tools\tests\core_vulkan_lifetime_test.c
cl /TC /W4 /WX /external:anglebrackets /external:W0 /O2 /Ilibretro tools\tests\core_d3d12_lifetime_test.c /link d3d12.lib dxguid.lib
```

`core_d3d12_video_test.c` links the production C89 frame driver and checks 25
frames byte-for-byte on the GPU. Coverage includes legacy structs against a
guard page, sparse v2 sync indices, delayed frontend consumers, required resource
states, padded and contiguous rows, resize, stale fence events and bounded
teardown with a blocked queue. It does not measure gameplay performance.

```bat
cl /TC /W4 /WX /external:anglebrackets /external:W0 /O2 /Ilibretro libretro\core_d3d12_fence.c libretro\core_d3d12_video.c tools\tests\core_d3d12_video_test.c /link d3d12.lib dxguid.lib
core_d3d12_video_test.exe
```

`dxil_writer_test.c` emits a compute shader directly from typed operations using
the owned DXIL/LLVM bitcode writer. It exercises integer and floating-point
arithmetic, dynamic control flow, phi nodes, casts, thread IDs and raw UAV
stores. It does not invoke a shader compiler or consume SPIR-V.
Vertex and pixel fixtures also check output-signature metadata and PSV tables.

On Linux, run:

```sh
make check-dxil-writer DXIL_VALIDATOR=/path/to/dxv
```

The check runs Microsoft's DXIL validator and requires its output to match the
native container byte for byte, including the checksum. The validator is a
development check, not a normal build dependency.

On Windows, compile `dxil_runtime_test.c` in a Visual Studio developer prompt:

```bat
cl /TC /W4 /WX /external:anglebrackets /external:W0 /D_CRT_SECURE_NO_WARNINGS tools\tests\dxil_runtime_test.c /link d3d12.lib dxgi.lib
dxil_runtime_test.exe build\make\host\dxil-writer-test.dxil
```

This creates a D3D12 WARP pipeline, dispatches the shader, copies its UAV to a
readback buffer and compares all 32 lane results against the expected values.
The authored test and support code use MSVC-compatible C89. The reused writer
implementation retains its original C code and license notices.

These foundation checks complement the integrated guest and helper stage suites below. The live D3D12 renderer uses the owned native DXIL path exclusively.

## Guest ALU instruction frontend

```sh
make check-dxil-alu DXIL_VALIDATOR=/path/to/dxv
```

This emits a shader directly from all 30 vector and 50 scalar ALU opcodes,
plus raw 96-bit Xbox ALU instruction snippets. Golden cases cover signed
zero, NaN, precision reduction, non-contracted multiply-add, dot-product
ordering, cube faces, predicate/address state, co-issue hazards, relative
addressing, source modifiers, scalar swizzles and mixed export masks.
The target also checks the authored frontend and fixtures as strict C89;
the reused bitcode headers retain their existing language requirements.

After compiling the Windows runtime fixture above, execute:

```bat
dxil_runtime_test.exe build\make\host\dxil-alu-test.dxil build\make\host\dxil-alu-test.dxil.expected
```

The fixture compares every result and state word, accepting any NaN payload
where the expected value is NaN. Integer state and signed zero are checked
exactly. The stage kill hook is instrumented in the compute fixture to verify
that inactive predicates suppress it; pixel-stage adapters must flush pending
exports and emit discard through this hook before destination writes.

The ALU frontend is independent of SPIR-V and shader-language compilers.
The live stage adapters connect it to texture policy, resource bindings, exports and pipeline creation; their integration suites are described below.

## Guest control-flow frontend

```sh
make check-dxil-flow DXIL_VALIDATOR=/path/to/dxv
```

Five raw guest programs cover nested calls and multiple returns, return without
an active call, dynamic loop counts, repeated loops, signed loop increments,
zero-count loops, predicate breaks, boolean and predicate branches, conditional
endings and loop-relative ALU operands. Each shader reports 88 output/state
words; use the Windows runtime fixture with `dxil-flow-N.dxil` and its
`.expected` file for N from 0 through 4. Clang and MSVC-generated containers
pass Microsoft's validator unchanged and execute with matching results on WARP.
The existing 1,256-word ALU execution check also passes after the shared test
storage refactor. Invalid target/source spans and missing required callbacks
are rejected by six negative fixtures.

Nonlocal guest edges use a program-counter dispatcher to keep the generated
DXIL control flow reducible. ALU and loop/call state survive backedges in
entry-block storage. Input words are decoded from the caller's span directly.
Fetch and memory-allocation callbacks are instrumented in these fixtures;
they do not implement real vertex/texture access. The live vertex, pixel and domain adapters provide those callbacks; their suites test the stage bindings.
## Guest vertex-fetch frontend

```sh
make check-dxil-vertex DXIL_VALIDATOR=/path/to/dxv
```

This checks raw instruction decoding, all 15 vertex formats declared in
`xenos.h`, four byte orders, signed and unsigned integer/normalized conversion,
both signed repeating-fraction modes, exponent adjustment, constant swizzles
and preserved lanes. Reserved swizzle 6 and unsupported formats are rejected.
Half-float conversion retains the existing Vulkan path's IEEE binary16
behavior; extended-range Xenos half floats are not implemented.

Full and mini fetches keep address, exclusive bound and byte order in runtime
storage. The memory adapter receives dword addresses and an in-bounds condition;
it must prevent unsafe reads. Only dwords referenced by the swizzle are loaded.
Predication suppresses reads and address-state updates. A full fetch with only
preserved lanes still updates the address for following mini fetches.

The execution fixture checks floor/nearest index conversion, signed offsets,
mirror-window masking, upper bounds, relative registers, fetch suppression and
load counts. It includes a raw guest program routed through the control-flow
frontend and vertex decoder. Run the Windows fixture with
`dxil-vertex-test.dxil` and `dxil-vertex-test.dxil.expected` to compare all 472
output/state words. Clang and MSVC generators pass validation and WARP execution;
the authored code also passes strict C89 and MSVC `/TC /W4 /WX` checks.

Fixture memory is a shader-local array. The live vertex/domain adapters use this frontend with shared-memory resources and native stage exports.
## Bound GPU resources

```sh
make check-dxil-resources DXIL_VALIDATOR=/path/to/dxv
```

The native resource helpers create DXIL handles, read raw buffers, and select
scalar words from dynamically indexed constant-buffer vectors. Guarded reads
branch around the buffer instruction and merge zero for inactive accesses.
The vertex adapter also rejects physical addresses outside the 512 MiB Xbox
window before converting dword addresses to bytes. The stage assembler remains
responsible for declaring resources and assigning registers/ranges.

The fixture declares an output UAV (`u0, space0`), a shared-memory SRV
(`t0, space0`) and fetch-constant CBV (`b4, space1`). A raw guest program
executes full and mini vertex fetches through the control-flow frontend,
using those actual bindings. Additional cases cross constant-buffer vectors,
exercise every vector component, and reject out-of-buffer, outside-window
and wrapping addresses. Malformed input types and resource indices are also
rejected before emitting invalid DXIL. The shader produces 128 output/state
words. Run it with the generated input files:

```bat
dxil_runtime_test.exe build\make\host\dxil-resources-test.dxil build\make\host\dxil-resources-test.dxil.expected build\make\host\dxil-resources-test.dxil.memory build\make\host\dxil-resources-test.dxil.constants
```

The Windows fixture reads input files directly into mapped upload buffers.
Its original two/three-argument invocations still use the single output UAV.
Clang and MSVC-generated resource shaders validate unchanged and execute on
WARP. The live stage adapters use these resource helpers; the integration suites below exercise complete guest shaders.
## Native texture sampling primitives

```sh
make check-dxil-texture DXIL_VALIDATOR=/path/to/dxv
```

The guest decoder extracts all eight texture opcode encodings, addressing,
swizzles, filters, LOD flags, signed bias in sixteenths and signed offsets in
halves. Decoding the packet does not yet implement all its guest semantics.
The native sampler primitive emits DXIL sampleLevel, sampleGrad, sample and
sampleBias operations with float results, validating stage, operand types,
dimensions and native integer offsets before emission. Implicit and bias
sampling are restricted to pixel shaders in the current Shader Model 6.0 path.

The compute execution fixture binds an RGBA32F 2D texture containing 4x4 and
2x2 mip levels, plus point and linear static samplers. Eleven cases compare
88 output/state words: point/linear filtering, explicit mip selection,
gradient-selected mips, linear mip blending, signed integer offsets and clamp
addressing. Run the Windows fixture with the generated texture file:

```bat
dxil_runtime_test.exe build\make\host\dxil-texture-test.dxil build\make\host\dxil-texture-test.dxil.expected build\make\host\dxil-texture-test.dxil.texture
```

Rows are read directly into mapped D3D12 upload footprints, then copied to the
GPU texture. The four-argument invocation selects this texture fixture; existing
buffer fixtures keep their original arguments. The separate
`dxil-texture-ps-test.dxil` checks implicit and bias sampling through the
validator only. Clang and MSVC generators pass both shader validations; the
2D compute fixture also passes WARP execution.

The primitive accepts 1D, 2D, 3D, cube and 2D-array views; execution coverage in
this fixture is 2D. The live texture stage adds guest coordinate/face conversion, half-texel offsets, signed/gamma treatment, sampler policies and register writes.
## Windows GNU compiler and ABI checks

`core_socket_unload_module.cc` exercises the production socket I/O service with
a pending UDP wait and a wrapper retained until DLL detach. The C89
`core_socket_unload_test.c` checks 64 shutdown/unload cycles, completion cleanup,
and actual module removal. `core_socket_worker_test.c` checks 128 cancellation,
join and retirement rounds, including reentrant removal. Compile its C driver
and `libretro/core_socket_worker.c` as C89 with `HAVE_THREADS`, linking the
owned common thread/eventcount implementations. The old static MinGW runner
reproduces a DLL-unload hang; run that negative control only in a supervised
test child with a timeout. These tests do not boot a title.

The following fixtures are authored in MSVC-compatible C89. They can also be
compiled with MinGW-w64 GCC using `-std=c89 -Wall -Wextra -Werror`.

- `core_events_test.c` checks coalesced title-stop requests and stale-state
  clearing. Run `make check-core-events` on Windows or Linux.
- `core_vblank_test.c` checks 20,000 frontend requests and 128 stop/join races
  without sleep or timer-driven wakeups. Compile it as C89 with
  `libretro/core_vblank.c`, `HAVE_THREADS`, the root/common include paths and
  the common thread/eventcount implementations.
- `context_switch_test.c` checks two fiber resumes using owned libco. Compile
  with `-Ithird_party/libretro-common/include -DLIBCO_MP` and link
  `third_party/libretro-common/libco/amd64.c`; use `aarch64.c` on ARM64 with
  `-D_GNU_SOURCE -DHAVE_POSIX_MEMALIGN=1`. The AMD64 source also builds with
  MSVC `/TC`, without a separate assembler.
- `core_fiber_test.c` exercises the production guarded-stack wrapper through
  `core_fiber_bridge.cc`, linked with the core's base/common libraries. It
  checks callback return, stack reuse, suspended restart/deletion, caller
  tracking, host-thread exit and migration, deep-stack growth with canaries,
  and eight concurrent workers.
  Compile the driver as C89 and bridge as C++20. On Linux, instrument the
  driver, bridge, `threading_fiber.cc` and `core_fiber_context.c` with
  `-fsanitize=address,undefined -fno-omit-frame-pointer`; run with
  `ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1` and
  `UBSAN_OPTIONS=halt_on_error=1`, then repeat with stack-use-after-return
  detection disabled. Linux sanitizer checks also verify retired fake-stack
  mappings are released. Dependency archives must use the same compiler ABI.
  `build_core_fiber_benchmark.cmd` and `run_core_fiber_benchmark.ps1` compare
  the production wrappers, extracting the old implementation from Git history.
  Results and limitations are in `results/fiber-9950x3d/README.md`.
- `d3d12_struct_return_test.c` checks five D3D12 C ABI adapters against real WARP
  objects. Compile it with `src/xenia/ui/d3d12/d3d12_struct_return.c`, add
  `-Isrc -isystem . -isystem third_party/DirectX-Headers/include`, and link
  `-ld3d12 -ldxgi -ldxguid -lole32 -luuid`. The C adapter object can also be
  linked to the fixture compiled as C++ to check calls from the renderer.
- `core_load_test.c` loads a native core, checks the 25 required libretro exports,
  API version and core name, then unloads it. Compile with `-isystem libretro`.
  Run `core_load_test.exe ./parallel_xbox360_libretro.dll`, then repeat with
  `lifecycle` as the second argument for two init/deinit cycles before unload.
  On Linux, compile with `cc -std=c89 -isystem libretro tools/tests/core_load_test.c -ldl -o core_load_test`
  and run `./core_load_test ./parallel_xbox360_libretro.so lifecycle`.

The Windows GCC build passed all these checks, including the context-switch
and D3D12 adapter tests compiled with MSVC. These are ABI and lifecycle smoke
checks; they do not exercise game execution or an actual title exit.

## ARM64 system-register checks

`arm64_sysreg_test.c` checks the owned C89 register-read helpers: counter
frequency is nonzero, consecutive counter values do not decrease, and reading
DCZID does not fault. The helpers use MSVC intrinsics, Clang ACLE intrinsics,
or GCC assembly for the same architectural registers.

For a Linux cross-build, compile with
`aarch64-linux-gnu-gcc -O2 -std=c89 -pedantic-errors -Wall -Wextra -Werror -Isrc tools/tests/arm64_sysreg_test.c -o arm64_sysreg_test`
and run `qemu-aarch64 -L /usr/aarch64-linux-gnu ./arm64_sysreg_test`.
The test also compiles as strict C89 with the Android NDK compiler. QEMU is
only a development test dependency, not part of the core build.

## Native geometry stages

`make check-dxil-geometry DXIL_VALIDATOR=/path/to/dxv` emits twelve native
geometry variants: points, rectangles, quads and scaled lines, each with a
minimal signature, six clip planes, or six cull planes. Large signatures
also carry sixteen interpolators, point size, point coordinates and vertex kill.
No SPIR-V is generated or consumed. The fixture also emits a matching native
vertex shader alongside each geometry container as `<path>.vs`.

Compile `dxil_geometry_runtime_test.c` with MSVC `/TC /W4 /WX` or MinGW GCC
`-std=c89 -Wall -Wextra -Werror`, linking D3D12 and DXGI. Run it with a generated
geometry filename and primitive type (1 through 4). It draws on WARP, reads
stream output, and compares the six triangle vertices and their attributes.
All twelve variants pass. The optional generator argument 1 injects a NaN,
2 sets one cull plane negative, and 3 supplies zero point size or a degenerate
line. Pass `discard` as the runtime test's final argument for these fixtures;
ten such cases pass with no stream output.

The authored stage/container, geometry, texture frontend and fixtures pass
strict C89 checks with GCC and MSVC. The native geometry generator is connected
to the D3D12 pipeline cache; game rendering has not been tested.
## Native EDRAM packing and dumps

`make check-dxil-edram DXIL_VALIDATOR=/path/to/dxv` emits the native format
fixture. Run `dxil_runtime_test.exe <shader> <shader>.expected raw` for exact
bit comparisons, including NaN payloads. Its 8,576 words cover all 1,024 7e3
encodings, saturation, ties, 20e4 depth remapping and integer-view formats.

Build `build/make/host/dxil-edram-dump-test` (append `.exe` on Windows).
Arguments are output path, resource format, depth flag, MSAA enum (0/1/2),
and layout configuration (0/1/2/3: native, scaled, native source in scaled
layout, native destination). All 168 combinations of the twelve color and
two depth formats, three MSAA modes and four configurations pass validation.
The 0/0/0/0 fixture also writes texture, expected and constant-buffer files.
Run `dxil_runtime_test.exe <shader> <shader>.expected <shader>.texture
<shader>.constants dump` to compare the 128 written EDRAM samples on WARP.
The unmodified columns outside that dispatch region are not compared.

For the MinGW runtime harness, add
`-isystem third_party/DirectX-Headers/include` and link D3D12, DXGI and dxguid.
Both GCC and MSVC harnesses pass the format and dump execution checks.
Standard dump generation is connected to the D3D12 render-target cache;
direct resolves and ownership-transfer shaders also use native emitters.
The dump generator accepts an additional `1` argument for a direct resolve.
All 168 direct variants also validate. The optional final case number 0..7
selects unchanged output, 8in16, 8in32, 16in32, red/blue swap, array slice,
array plus byte swap, or half-pixel fill. Format 0 emits executable 32bpp
data; format 15 with case numbers 8..15 emits executable 64bpp float data.
These fixtures initialize the output buffer to zero and compare all words,
including untouched bytes, using the runtime harness's `resolve` mode.
All four scaling configurations pass all eight cases for both pixel widths:
64 WARP executions. Cases with 64bpp data also cover low-half red/blue swapping
and the 8in64 dword exchange. The runtime fixture compiles with GCC and MSVC.
D3D12 dump and direct-resolve generation now call the native emitter directly;
ownership transfers now do the same.
`make check-dxil-pixel-interface DXIL_VALIDATOR=/path/to/dxv` checks float and
integer color outputs, unpacked depth/stencil outputs, and per-sample input
metadata needed by ownership transfers. All five validate and pass WARP pipeline
creation. For that pipeline check, place a compatible native VS from the geometry
fixture at `<pixel-path>.vs` and run `dxil_geometry_runtime_test.exe <pixel-path>
<mode> pixel`. This checks pipeline creation, not pixel execution.
`make check-dxil-edram-transfer DXIL_VALIDATOR=/path/to/dxv` validates all
10 ownership-transfer modes across 36 MSAA/scaling configurations, plus all
144 color-format pairings, 136 depth/stencil pairings and six policy variants. Depth and stencil SRVs are declared only when read.
Policy flag bits select no-discard stencil (1), 7e3 value conversion (2) and pixel-converted depth (4). The fixture arguments are `<output> <mode> <variant> [source-format dest-format [flags]]`.
`dxil_edram_transfer_runtime_test.c` executes color-to-color transfers on WARP.
Compile as C89 with the Windows SDK, linking d3d12.lib and dxgi.lib. Run it with
`<pixel-shader> <native-geometry-fixture.vs> <config>`; configs 0..3 correspond
to generator variants 0, 9, 18 and 27. Each compares every rendered float bit
for unchanged, scaled-to-native and native-to-scaled layouts. These four runtime
checks cover color preservation and coordinate remapping; the other modes
currently have container validation coverage rather than runtime comparisons.
`make check-dxil-depth-only DXIL_VALIDATOR=/path/to/dxv` validates six native
host-depth shaders: unchanged depth, float24 truncation and float24 rounding,
each with or without coverage counting. The generator arguments are
`<output> <depth-mode 0..2> <count-coverage 0..1>`.
Compile `dxil_depth_only_runtime_test.c` as MSVC C with d3d12.lib and dxgi.lib.
Run `<pixel-shader> <native-geometry-fixture.vs> <depth-mode> <count> <samples>`.
Samples may be 1, 2 or 4. An optional `disabled` or `slot3` checks counter
suppression or the 16-byte query stride. A final case number 0..10 selects
independent depth-bit expectations for underflow, halfway ties and clamping;
use `open` before the case number for the default counter slot. Single-sample
runs compare the rendered D32 depth bits; all runs compare the full counter
buffer, including untouched lanes. MSAA depth values are not read back.
The native host-depth and ROV depth-only shaders are connected to pipeline initialization.

`make check-dxil-rov DXIL_VALIDATOR=/path/to/dxv` checks the native ordered-buffer
pixel shader and the depth/stencil compute fixture. The latter emits 49,152
runtime input records covering compare functions, all stencil operations,
read/write masks, clamp/wrap boundaries and disabled tests. Execute it with
`dxil_runtime_test.exe <shader> <shader>.expected <shader>.memory <shader>.constants`.
All 393,216 output words match the independent CPU reference. Packed EDRAM words
are compared exactly, including patterns that resemble floating-point NaNs.

`make check-dxil-rov-depth DXIL_VALIDATOR=/path/to/dxv` checks 24 native ROV
containers across guest MSAA, scaling, survey mode and failure-counter policy.
The generator arguments are `<output> <msaa 0..2> <scale-config 0..1> <survey 0..1>
[full-counters 0..1]`. It builds and releases the same owned shader set used by
the renderer. Unorm and float24 conversion use separate control-flow paths.

The C89 Windows harnesses compile with either MSVC and the Windows SDK, or GCC
with `-isystem third_party/DirectX-Headers/include`; link D3D12 and DXGI (plus
dxguid with GCC). `dxil_rov_runtime_test.exe <shader> <native-geometry-fixture.vs>
[layers]` checks ordered updates at 64 pixels, including 1,024 overlapping layers.
`dxil_rov_depth_runtime_test.exe <shader> <native-geometry-fixture.vs> <msaa>
<scale-config> <scenario> <survey> [flags]` compares every EDRAM word and counter
lane. Scenarios 0..9 cover pass/fail, stencil masks, disabled tests/counters,
float24, per-face state, constant and slope polygon offset. Optional flags may
contain `reverse`, `partial`, `slot3` and `nofail`; partial coverage uses
scenarios 0..6. `nofail` requires a shader generated with full-counters=0.
The final 324-run matrix covers both full-screen and partially covered triangles,
1x/2x/4x guest samples, native and 2x3 scaling, both faces, counter suppression,
survey flags and nonzero query slots. Guest shaders, including their ROV color
and export paths, and linked tessellation stages still need native integration.
`make check-dxil-program DXIL_VALIDATOR=/path/to/dxv` validates nine complete
raw-microcode programs through the production program layer. These cover sparse
float constants (including indices 65 and 255), runtime relative clamping, bool
and loop CBVs, calls, full/mini vertex fetch, invalid fetch bounds, texture LOD/
gradient state and serialization callbacks, and true/false predicated partial
exports. Initialization does not report guest writes. Only referenced export
slots are initialized; never-written slots read as zero. The sparse/loop sample
containers shrink from 11,732/12,660 to 5,556/6,264 bytes with this preparation.

Run `dxil_runtime_test.exe <shader> <shader>.expected <shader>.memory
<shader>.constants program` to compare all 32 words per fixture exactly.
The program runtime mode binds three CBV views of the fixture upload: float
constants at byte 0 (b1 space1), bool/loop constants at 4096 (b3 space1), and
fetch constants at 4352 (b4 space1). Both GCC and MSVC harnesses execute the
programs. The texture fixture instruments the stage-policy callback; it does
not test native texture policy or replace the existing texture sampling tests.
The native plain vertex adapter connects this layer to renderer compilation.
Memory-export allocation, pixel kill handling and texture policy use native
stage callbacks in the renderer.

`make check-dxil-guest-vertex DXIL_VALIDATOR=/path/to/dxv` validates six complete
native vertex containers. The C89 adapter uses SV_VertexID, raw shared memory
and the same system/float/bool-loop/fetch CBV bindings as the renderer. Default
point-size initialization does not report a guest export write.

Build `dxil_guest_vertex_runtime_test.c` with the same MSVC/GCC Windows harness
settings above. Run `<exe> <shader> <config 0..5> <flags 0..7> <endian 0..3>
<signed-base> <kill 0..3>`. Flags select W-not-reciprocal, XY-divided-by-W and
Z-divided-by-W. Kill values select zero, one, negative zero and a raw denormal.
The 210-case WARP matrix covers all flags in all six configurations, each
endian mode with bases -1/0/1/16777216, and nonzero kill values across the three
kill-enabled configurations. Stream output is compared against independent CPU
position, clip/cull and attribute calculations, including sparse raw exports
0/3/15 densely packed into TEXCOORD0/1/2. Six runs also pass through the MSVC
harness. These are GPU execution fixtures, not game-title compatibility tests.

`make check-dxil-guest-pixel DXIL_VALIDATOR=/path/to/dxv` builds 476 complete
pixel fixtures and their vertex companions. All seven host depth modes are
combined with explicit depth exports, generated parameters, centroid/precise
interpolation, sparse interpolators, native 2x sample mapping, conditional RT0
exports and all supported source blend-premultiplication factors.

Build the C89 `dxil_guest_pixel_runtime_test.c` harness with the Windows settings
above, linking D3D12 and DXGI (plus dxguid with GCC). Run `<exe> <pixel.dxil>
<vertex.dxil> <mode 0..475> <scenario 0..39> [guest-msaa 0..2]`. A fixture's `.vs`
companion is a simple varying producer; `.guest.vs` executes real guest vertex
microcode, including vertex fetch and ALU work. Point-parameter modes 84..111
use the simple producer; actual primitive-expansion coordinates have their own
geometry tests. The harness aliases POSITION/TEXCOORD input declarations onto
the same vertex bytes to accept the native vertex fixtures.

Modes use groups of 28: seven depth modes, with/without pixel parameters and
with/without explicit depth exports. Groups 0..6 select the base, centroid,
barycentric, point-coordinate, sparse-interpolator, native-2x and conditional-RT0
interfaces. Groups 7..16 pair the ten supported RGB/alpha premultiplication
factors. Scenarios 0..7 select normal, failed-alpha, ALU-kill, gamma, exponent
bias, alpha-to-mask, disabled-counter and nonzero-counter-slot execution.
Scenarios 8..39 check every alpha comparison function with equal, NaN, greater
and less inputs. NaN not-equal and always pass; skipped RT0 exports bypass its
alpha tests. Resolved MSAA colors use a small floating tolerance; the NaN alpha
resolve lane is unspecified and excluded, while other colors and exact query
counts still verify the comparison result. Single-sample depth words and query
counters are checked exactly.

The 6,608-case matrix covers all base interfaces, native/emulated 2x and 4x MSAA,
conditional exports, representative blend/depth combinations and alpha cases.
The fixed guest-stage ABI uses TEXCOORD rows 0..16, position row 17, packed
clip/cull rows 18..19 and generated front-face row 20. Signature tables sort by
hardware register while preserving metadata name/declaration order. Native VS/PS and VS/GS checks cover independent compilation,
including six clip planes plus a cull distance. Native shader model 6.1 adds
barycentrics and attributeAtVertex without an LLVM compiler dependency.

`make check-dxil-render-color DXIL_VALIDATOR=/path/to/dxv` validates all twelve
numeric render-color codecs. The fixtures compare 2,359,296 exact GPU output
words with `dxil_render_color_reference.h`, an independent C89 CPU reference.
They exhaust the signed fixed-point and extended-half input encodings, plus
half conversion midpoints, both signs, saturation, infinities and NaNs. The
extended half exponent 31 represents finite values, not IEEE infinity.
Run the Windows `dxil_runtime_test.c` harness as `<exe> <shader> <shader.expected>
<shader.memory> <shader.constants> resource-raw` for exact resource comparisons.

`make check-dxil-guest-rop DXIL_VALIDATOR=/path/to/dxv` validates 504 complete
ROV pixel containers across twelve color formats, 1x/2x/4x samples, native and
2x3 resolution, blending specialization, full/partial query counters, explicit
depth, centroid/barycentric/sparse inputs and conditional outputs. Shader
creation, async compilation and cache warm-up all use the native adapter.

Build the C89 `dxil_guest_rop_runtime_test.c` Windows harness with D3D12/DXGI
(and dxguid with GCC). Build `dxil_guest_rop_reference_test.c` with a host C
compiler (and libm on Unix). Generate a pixel fixture using
`dxil-guest-pixel-test <shader> <mode> <config>`, then its CPU data using
`dxil-guest-rop-reference-test <shader> <mode> <config> <scenario>`.
Run `<runtime> <shader> <shader.vs or shader.guest.vs> <config>`.
The reference accepts modes 0/14/168/182 (plain/explicit-depth, each with or
without conditional RT0). Config bits 0..1 select MSAA, bit 2 selects 2x3
scaling, bits 3..6 rotate the format list, bit 7 disables blending, bit 8
omits failure counters, and bit 10 adds partial write masks.

Scenarios 0..12 cover overwrite, partial/all masks, full EDRAM wrap, passing
and failing depth/stencil, kills, alpha rejection, exponent bias, alpha-to-mask
and disabled queries. Scenarios 13..15 combine rejection with failed tests to
check early query accounting. Scenarios 16..1140 enumerate all 15x15 factor
pairs and five equations, using reversed RGB/alpha factor pairs. Scenarios
1141..1144 check float24, constant offset and front/back slope offset. Every
EDRAM word and all 64 query-counter words are compared exactly, including
untouched memory. These fixtures exercise guest ALU/control flow and the
native render backend; they do not establish game-title compatibility.

The verification batch also passed 3,375 blend-factor/equation executions,
1,152 real guest-vertex/ROV executions, 900 masked MSAA blend executions,
288 no-blending/partial-counter executions, 48 float24/polygon-offset cases,
18 MSVC ROV executions and 368 host-render-target regression executions.
GCC and MSVC C89 checks pass; the Windows GCC core builds and passes ABI
load/unload and two initialization/deinitialization cycles.

`make check-dxil-bindless check-dxil-bindless-array DXIL_VALIDATOR=/path/to/dxv`
validates native SM 6.6 descriptor-heap sampling. The C89 fixture uses a bound
raw SRV at t3 (matching the renderer's pixel descriptor-index buffer), a CBV,
a raw UAV, and direct resource/sampler heaps. It uses the renderer's two-dword entries
with samplers following textures. It covers four nonzero/zero
texture indices, mip-restricted views, point/linear samplers and uniform/nonuniform
index annotations. The module keeps CBV/sampler/SRV/UAV PSV records in validator
order, and bound handles use createHandleFromBinding plus annotateHandle.
Barycentric setup preserves a previously selected newer shader model.
Run the Windows `dxil_runtime_test.c` harness with `<shader> <shader.expected>
<shader.texture> <shader.constants> heap` (or `heap-array`). Each fixture checks
64 exact output words. The harness also reports D3D12 debug-layer failures.

`make check-dxil-texture-weights DXIL_VALIDATOR=/path/to/dxv` checks four shaders totaling 256 numeric
query cases and 2,048 exact GPU output words via the `resource-raw` harness mode.
Sizes include wide 1D textures through 2^24, all dimensional packing layouts,
normalized/unnormalized coordinates, negative/outside coordinates and signed
half-texel offsets. This follows the existing core's base-level query policy:
unused coordinates and the mip-filter weight are zero.

`make check-dxil-guest-texture-query DXIL_VALIDATOR=/path/to/dxv` validates 84
complete pixel query containers (modes 476..559), two vertex query containers
(configs 6/7), and checks rejection of malformed query encodings and unsupported vertex derivatives. There is no intermediary compiler fallback.
Pixel groups 17/18/19 select normalized weights, derivatives and unnormalized
weights; they export results through RT3. The 1,344-case HRT matrix uses real
guest and synthetic vertex producers, alpha/kill/gamma/bias/coverage cases and
all host-depth modes. Vertex stream-output checks cover 256 combinations of
position flags, endian modes and signed/large index bases.

The ROV reference/runtime harnesses also accept query modes 476/504/532.
Their 234-case matrix covers all twelve render formats, native/2x3 resolution,
masking, exponent bias and depth/stencil/query outcomes. Packed results and
query counters are exact. Changed floating RT3 query outputs allow a small
interpolation/derivative tolerance; untouched memory still compares exactly.
Derivative bias cases avoid packed-format quantization ties, since hardware
interpolation can differ slightly from the analytic CPU gradient.
The query fixtures above are separate from the native sampling integration below.

The native numeric fixtures split their cases across four small shaders to
avoid excessive driver JIT latency. The harness allows a bounded 50-second
GPU wait for loaded build machines. SM 6.6 pixel validation also combines
direct heaps with barycentrics to check that version selection is preserved.

## Integrated native guest 2D sampling

`make check-dxil-guest-sampling DXIL_VALIDATOR=/path/to/dxv` validates 62
C89 sampling-policy containers, 504 complete guest pixel shaders (560..1063),
and nine guest vertex shaders (9..17). All containers must validate
without changes. Shader logic is separately checked as strict C89.

Run each policy container with `dxil_runtime_test.c` using arguments
`shader.dxil shader.dxil.expected shader.dxil.texture shader.dxil.constants heap-policy`.
These fixtures check point/linear sampling, mip and register LOD, instruction
and fetch biases, explicit gradients with independent H/V biases, normalized
and unnormalized coordinates, locked mip sizes, offsets, sign-view selection,
integer and gamma conversion, exponent and filter overrides. The policy harness
allows a 1e-6 relative float tolerance for hardware filter operation ordering;
other heap tests retain exact comparisons.

The guest pixel runtime accepts modes 560..643 with scenarios 0..7 and the
`.guest.vs` companion. It checks four targets, depth and coverage after actual
raw guest texture instructions, including normalized, unnormalized and computed
LOD with barycentric interpolation. Vertex runtime configurations 9/10 check
normalized/unnormalized sampling through stream output, preserving positions
and the export observer. Both use shader-model 6.6 dynamic resource/sampler
heaps and the renderer's two-dword descriptor-index entry ABI.

Renderer sampling covers 1D, 2D, 3D/stacked and cube, including scaled draws. Memory-export and tessellation integration have separate suites below. `core_zstd_test.c` checks checksum-bearing frames through the Zstandard adapter,
the XXH64 trailer and corrupt-checksum rejection. Hash timings and limits are
recorded in `optimization_measurements.md`.
These tests do not establish game compatibility.

`make check-dxil-guest-sampling-rov` validates 144 complete sampled/query ROV shaders.
ROV reference/runtime harnesses accept sampling modes 560/588/616 at unit
resolution. Generate with `dxil-guest-pixel-test shader.dxil MODE CONFIG`,
then `dxil-guest-rop-reference-test shader.dxil MODE CONFIG SCENARIO` and
`dxil-guest-rop-runtime-test shader.dxil shader.dxil.guest.vs CONFIG`.
Checks include all twelve formats, 1x/2x/4x coverage, masks, depth tests,
kills and exponent adjustment. Every EDRAM word and query counter is checked.

## Native 1D and cube guest sampling

The guest-sampling target also validates policy modes 30..44, guest pixel modes
644..783, and vertex configurations 11..13. Vertex configurations 11/12 check
cube explicit/gradient containers; configuration 13 executes 1D sampling through
stream output. Pixel groups 23/24/25 check 1D, wide 1D, and promoted wide 1D
with computed gradients. Groups 26/27 check cube sampling with explicit and
automatic LOD through the full guest pixel adapter.

`make check-dxil-cube-sampling DXIL_VALIDATOR=/path/to/dxv` validates 20 cube
policy containers. Run them with the generic GPU harness using
`shader.dxil shader.dxil.expected shader.dxil.texture shader.dxil.constants heap-cube`.
The file contains six distinguishable faces, each with two distinguishable mips.
Checks cover every face orientation, clamped face indices, offsets, unnormalized
coordinates, signed-view selection and explicit/register-gradient LOD.

The 1D policy fixtures use the existing four-by-four host fixture to isolate
coordinate remapping independently of physical allocation size. They check both
sides of a wide row boundary, the row cap, negative coordinates and explicit
gradients; full-size texture-cache allocation is not covered by these fixtures.
The complete pixel tests use 1D/wide metadata with the same small image to verify
raw microcode dispatch, policy output and native pixel epilogues together.

Cube policy case 19 samples both an array view and a cube view of the same
resource in one shader, with shared fetch metadata and deduplicated samplers.
Run it with `heap-mixed`; the other cube cases use `heap-cube`. This checks
mixed resource annotations and descriptor offsets, not just isolated view types.
Metadata reads reuse their 16-byte CBV vector in the entry block and extract
constant lanes; the policy and full guest execution suites cover the result.

## Native volume sampling and computed LOD

`make check-dxil-volume-sampling DXIL_VALIDATOR=/path/to/dxv` validates 32
policy containers. The generic runtime accepts `heap-volume` for cases 0..15
and `heap-stack` for 16..31. Actual four-slice 3D resources and four-layer arrays
have distinguishable data in every slice and mip. Fixtures cover normalization,
Z offsets, explicit and register-gradient LOD, hardware 3D filtering, manual
layer filtering, fetch-constant filter overrides, absolute positive/negative
layer gradients, clamped edges, zero interpolation weight, signed views and
exponent adjustment. Uploads go directly into mapped GPU footprints.

Full guest pixel modes 784..895 cover explicit/automatic 3D and stacked sampling;
stacked outputs interpolate between distinguishable layers. Modes 896..1035
cover computed LOD for 2D, 3D, cube and stacked views, including a signed view
with a different most-detailed mip. These use the native unclamped LOD operation;
query X allows 0.008 LOD error for hardware LOD precision. Other query lanes
remain exact zero. Floating stacked outputs allow a 2e-6 relative tolerance for
interpolator/filter ordering; untouched targets, depth and counters stay exact.

Modes 1036..1063 verify the current zero-result border-fraction policy. This
instruction preserves the existing Vulkan zero-result behavior. Explicit-gradient or non-pixel computed
LOD queries also preserve the existing zero-result policy and skip source reads.
Vertex configurations 14/15 execute 3D explicit/gradient fetches through stream
output; 16/17 execute descriptor-free zero LOD/border results.

The ROV reference/runtime also accept mode 896 for computed LOD. All twelve
packed/floating color formats, 1x/2x/4x coverage, masks, depth failures, kills and
exponent adjustment can be compared over the complete EDRAM and counter buffers.
Native volume filtering chooses minification using the absolute layer-gradient
magnitude greater than one and clamps linear layer edges without blending the
last two layers spuriously; these are covered by the independent numeric oracle.

`make check-dxil-scaled-lod` also validates query-only ROV shaders at 2x3 draw
resolution and 1x/2x/4x coverage. The resource layout records actual sampling
separately from querying; scaled queries and sampling both compile natively.
Signed fixed-point and floating targets make
the negative queried LOD observable rather than clamping every result to zero.
For floating LOD outputs with discontiguous write masks, the runtime checks
preserved bits exactly and verifies that written bits can originate from the
allowed LOD interval. A one-ULP intrinsic difference can otherwise create a
large post-mask float difference. Other packed outputs and counters stay exact.

The sampling targets also compile the texture emitter with optimization and strict
C89 warnings via "check-dxil-texture-stage-c89", catching definite/possible
initialization diagnostics that a syntax-only check may not report.

`make check-dxil-scaled-sampling` validates 144 ROV shaders covering 2x3 draw
resolution, resolved and ordinary resources, three color formats and 1x/2x/4x
coverage. Configurations add 2048 for affine coordinate bias, 4096 for an 8x12
resolved resource, and 8192 for a coordinate MOV. Further flags select a skipped
predicate (16384), executed predicate (32768), Z-only write (65536), relative
write (131072), or mixed per-pixel predicates across two clauses (524288).
The latter predicate changes provenance during the same draw. Flag 262144
disables tracking as a negative control and must fail the biased point oracle.
Run each generated shader through the ROV runtime/reference with scenarios
0, 1, 4, 5, 8 and 10 to compare all EDRAM words and query counters (864 draws).
Policy cases 45..61 cover scaled sizes, resolved masks, disabled scaling,
normalized/unnormalized coordinates, locked mips, gradients, half/whole offsets,
point-footprint clamping, invalid provenance and linear-filter bypass.

### Native guest memory exports

`make check-dxil-memexport DXIL_VALIDATOR=/path/to/dxv` checks direct C89
packing/storage, complete guest programs and vertex/pixel adapters. The target
also performs optimized strict-C89 compilation. Export format numbers follow
`xenos::ColorFormat`, distinct from EDRAM render-target formats. All 26 existing
formats and aliases are covered, with signed/unsigned normalized or integer
conversion, extended finite half exponents, NaN flushing, raw float32 payloads,
red/blue swapping and all six guest endian modes. Integer test values stay within
the defined conversion range. Equivalent formats share generated shader bodies.

The fixture accepts `dxil-memexport-test output config [reference|program|vertex|pixel] [variant]`.
A config packs format in bits 0..5, signed in bit 6, integer in bit 7,
endianness in bits 8..10 and red/blue swap in bit 11. Policy bits 12+ select:
0 full writes, 1 sparse dirty mask, 2 shortened stream, 3 exhausted stream,
4..7 invalid address components, 8 disabled invocation, 9 invalid format,
10 mirrored physical address, 11 zero stream length, 12 tail element,
13 no dirty elements, and 14 only eM0/eM1 dirty.
`reference` writes independent CPU output/constants without regenerating DXIL,
allowing all 1,248 format/numeric/endian combinations to execute one dynamic
shader. Policies 0..14 add 390 bounds/dirty/address cases over the 26 formats.

Program/stage variants cover ordinary exports (0), skipped predicated eM writes
(1), a true kill after eM0/eM1 (2), a false kill (3), ALLOC reset with no new eA
(4), and ALLOC reset followed by a new eA (5). Program variant 8 verifies an
empty memory ALLOC with no export state. Pixel variants 6/7 use 2x3 draw scaling
and place the invocation at an allowed/disallowed host pixel respectively.
Pixel variants 9/10 combine exports with ROV EDRAM or HRT counter resource
declarations, checking class-local UAV IDs without modifying those auxiliary
buffers. The program commits pending elements before true kills, then suppresses later
writes; false kills retain them. Runtime dirty masks reflect actual predicates.
Untouched eM elements skip initialization and loads, with no staging copies.

On Windows, build `dxil_memexport_runtime_test.c` as C89 with D3D12, DXGI and
DXGUID libraries. Run `dxil-memexport-runtime output` to execute the generated
`.vs`/`.ps`, `.constants` and `.system` companions and compare all 128 shared-memory
bytes to `.expected`, including untouched sentinel bytes. An optional second
argument selects another shader/system prefix, so many independent references
can share one pipeline variant. D3D12 debug errors fail the run. This fixture
covers vertex and pixel UAV writes directly; it is not gameplay verification.
Direct compute/program containers use the existing `dxil-runtime` helper with
`shader expected input constants resource-raw`. The graphics stage deliberately
uses UAV loads for shared-memory reads during export draws, avoiding SRV access
while the backing resource is in its write state.

### Native guest domain shaders

`make check-dxil-guest-domain DXIL_VALIDATOR=/path/to/dxv` checks 90 domain
containers and optimized C89 compilation. Modes 0..3 select triangle CP/patch
and quad CP/patch layouts. Add 4 for continuous tessellation or 8 for adaptive
(patch modes 9 and 11 only). Flags 16/32 add an actual texture fetch or memory
export, 64 adds six clip planes, and 128 adds cull planes, point size and vertex
kill output. Flag 240 combines all those interfaces. Texture variants execute
a raw fetch followed by a raw guest ALU export of the sampled register.

The `dxil-guest-domain-test prefix config` fixture writes `.ds`, `.system`,
`.constants`, `.expected` and `.hosts`. The latter contains two 32-bit asset
indices from the same C89 selector the pipeline uses. In order, the assets are
indexed/adaptive tessellation VS, discrete triangle1/triangle3/quad1/quad4 HS,
continuous equivalents, and adaptive triangle/quad HS, all under
`src/xenia/gpu/shaders/bytecode/d3d12_dxil/`. Copy the selected raw assets to
`prefix.host.vs` and `prefix.hs`.

Run `dxil-memexport-runtime prefix prefix mode` on Windows (add 16 to `mode`
for texture-bearing variants) to capture domain
output through stream output. The fixture writes guest r0/r1 and a separate
hardware domain-location probe. Every captured vertex must match the exact
triangle ZYX or quad/patch register layout and the processed control-point or
patch indices. All corners must appear, and vertex counts must match the chosen
tessellator partition. Factor-2 fractional-even triangle patches form six
triangles around a center; factor-2 quads form an eight-triangle grid. The GPU
matrix covers ten supported mode/layout combinations, each with ordinary,
clip-plane and cull/kill/point-size interfaces, with texture and memory-export
combinations (90 executions). Textures use the native resource/sampler heaps
and raw index-buffer binding, with all four sampled values checked per vertex.
Uploads go directly into a mapped GPU texture footprint. Memory exports write
a known float4 into a separate UAV; all 32 words
are checked, including untouched sentinels. Stream output has its own buffer
and resource state. These synthetic checks do not establish game compatibility.
The raw host helpers use b0/space0 with pinned system offsets; guest stages use
b0/space1, so both root bindings alias the same existing constant upload.
There is no intermediate SPIR-V conversion or owned copy of the static helpers.
Line-domain tessellation remains unimplemented as in the existing Vulkan path.

`core_file_test.c` exercises the production C89 stream owner through a simulated
frontend VFS: seven-byte short reads, adjacent-request seek elision, failed seeks,
read errors, zero progress, interrupted-read retry, invalid/overflow ranges,
empty/null spans and recovery after failure. Native tests verify mapped views
where available and random reads through the compiled no-mapping fallback.
`core_zarchive_test.c` additionally runs the real writer/reader/cache against
both native input and a frontend VFS that forces short reads and disables
borrowed mappings. Compare exact source bytes through compressed/raw blocks,
cross-block random reads, cache eviction and empty files. New I/O algorithms
are C89; the existing archive classes retain minimal C++ ownership adapters.

`core_zarchive_format_test.c` independently constructs the byte format instead
of relying on the existing writer as its oracle. It checks native borrowed
metadata, allocation-free mapped raw reads, short-read VFS fallback, four
concurrent raw/compressed readers, collision-heavy cache churn, invalid ranges/names/graphs, and private publication after failed
reads/decodes/checksums. `XE_ZAR_TEST` enables storage queries only in test
builds. Strict MSVC and MinGW C89, Android syntax, Linux ASan/UBSan and TSan
cover the reader. On this WSL host TSan needs per-process ASLR disabled with
`setarch x86_64 -R`; this changes neither production flags nor system policy.
The independent real-writer fixture still checks compatibility, long names,
compressed/raw blocks, mixed cache backings, random reads, eviction and empty files.

`run_zarchive_benchmark.sh` builds the exact historical reader at `3ca00408`
and the current production reader as separate shared objects, with matching
Make-shaped flags and symbol binding. Its C89 process driver keeps a common
rthreads worker parked to exercise libc's multithreaded mutex path, checks
returned bytes, compares name-based and already-resolved-handle reads, and
records peak resident memory. It runs serial alternating process pairs,
identical-baseline controls and an opaque-ABI copy-only reference on one guest
CPU. The latter is a measured sustainable data-movement reference, not a claim
of the hardware's theoretical peak. Assembly and machine/compiler/source
identification are saved alongside raw CSV samples. Run from Linux after other
builds/tests finish:

```sh
bash tools/tests/run_zarchive_benchmark.sh
```

`BENCH_CPU`, `BENCH_PAIRS`, `BENCH_CONTROL_PAIRS`, `BENCH_FLOOR_PAIRS`,
`BENCH_CACHE_ITERS`, `BENCH_MISS_ITERS` and `BENCH_OPEN_ITERS` allow controlled
experiments. Defaults use 10 comparison pairs, 5 identical-baseline pairs,
10 reference processes, 30 million cached/reference reads, 1.2 million misses
and 300 opens. This is a cache-hot CPU/DrvFS component experiment; it does not
measure cold storage, frontend throughput or game frame rates. The C89 driver
also compiles for Windows, using native loader, profiling and resident-memory
APIs. The shell runner is Linux-specific.

The [3 November measurement report](results/zarchive-2026-11-03/README.md) includes raw samples, source/compiler identification, assembly and the remaining gap to the copy reference.


`core_xex_metadata_test.c` exercises the production allocation-free C89 parser with
memory views and exact-read callbacks: both XEX versions, execution and ratings
fields, first-duplicate precedence, embedded NULs and trailing padding, malformed
counts/offsets/lengths, short sources and failed reads. Failure must leave output
unchanged. A sparse source declares a 4 GiB header while asserting that fewer than
256 bytes are read; its callback rejects any access outside the actual fixture.
This establishes bounded traffic for that fixture, not gameplay throughput.

Example from the repository root (use an available temporary directory):

```sh
cc -std=c89 -pedantic -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -I. libretro/core_xex_metadata.c tools/tests/core_xex_metadata_test.c \
  -o core-xex-test
./core-xex-test
```

MSVC accepts the same two sources with `/TC /W4 /WX /I.`.

`core_sha256_test.c` checks owned common SHA-256/SHA-224 known answers,
million-a, every split of the standard messages, unaligned inputs, padding
boundaries, raw compression-block semantics and SHA-1 single/two-message paths.
Compile `hash/lrc_hash.c` and the test as C89 with the owned common headers;
link `retro-common` for its CPU/file helpers. MSVC `/TC /MD /O2 /W4 /WX` and
MinGW are covered. For a forced scalar check, compile the implementation with
`cpu_features_get=xe_test_cpu_features_get` and the test with
`XE_SHA_TEST_SCALAR`. The native CPU probe remains unchanged in production.
Linux ASan/UBSan exercises the real source, CPU probe and test. The real-writer
archive test independently checks the footer's byte coverage and zeroed digest
field through both native streams and frontend VFS with short reads.

`core_sha256_benchmark.c` compares exact historical private SHA and current
common streaming SHA. Define `XE_SHA_BASELINE` for the former, and provide its
historical header and source extracted with `git show`. The scalar benchmark
uses the same test-probe flags above. See [recorded SHA-256 observations](results/sha256-2026-11-03/README.md)
for raw samples, compiler/host conditions, reproduction and limitations.

`core_xex_metadata_bridge.cc` is minimal C++ interoperability for the existing
metadata result. Compile the C89 metadata test with `XE_XEX_CPP_BRIDGE`, link
the bridge and production `src/xenia/vfs/xex_metadata.cc`, and define
`FMT_HEADER_ONLY` or link fmt. The bridge verifies mapped-memory and exact-read
projection into the final metadata/string, including an embedded NUL. Linux
function sections plus linker garbage collection allow testing these overloads
without the unused native-path adapter and filesystem dependency graph.

`core_crypto_test.c` verifies the C89 account-crypto boundary using independent
HMAC-SHA1 known answers (including a key longer than one block), all three-span
split positions, every output length through 25 bytes, output canaries, input/key
aliasing and invalid-span failure. A 128-bit RC4 known answer, exact in-place
round trip, short keys/output and both partial-overlap directions cover the
account-file cipher. Full HMAC digests go directly to caller storage; truncated
digests require only 20 bytes of scratch. The account decryptor retains its
private verification buffer so failed authentication cannot publish partial data.

`core_sha1_test.c` with the minimal `core_sha1_bridge.cc` compares the previous
TinySHA1 implementation with the production common one-shot and streamed hashes.
It tests all lengths below 1024, unaligned input and additional lengths through
1 MiB. The historical header is extracted by `run_guest_sha1_test.sh` from
Git into the test output directory and is absent from production. These are byte-compatibility
checks, not gameplay or throughput measurements.


`core_logger_apc_test.c` exercises the real DLL and logger through the public
libretro API. It holds the writer in a frontend log callback, waits for the
other core workers to stop, and queues an APC to the thread closing the core.
An alertable join returns before the writer exits; the test records that failure
while leaving the writer parked until process termination to avoid deliberately
accessing freed memory. A correct join defers the APC, waits for the released
writer, and permits safe DLL unload. The queued test APC is then explicitly
delivered through a bounded alertable event wait. The installed b66630a MinGW
DLL failed with lifetime error 4; the fixed MSVC and MinGW builds pass.

Build as C89 with the libretro include directory and run:

```
core-logger-apc.exe <core.dll> <private-system-directory> <invalid-content.xex>
```

The non-Windows build reports that the Windows APC test is skipped. The existing
content-close and Vulkan/D3D12 lifetime tests cover the ordinary shutdown path.

`run_guest_sha256_test.sh` compares the C89 adapter with the historical private
SHA-256 class extracted from commit `183a97d1`. Git extraction is developer-only;
the core build needs neither Git extraction nor that reference implementation.
The oracle's unaligned word loads are changed to memcpy only in the test build.
The C89 driver compares complete guest states and surrounding guards after each
operation, including unaligned input/state, split blocks, truncated and aliased
outputs, repeated finalization, update after finalization and 32-bit count wrap.
It also checks the independent SHA-256 `abc` known answer. Linux enables
ASan/UBSan with failure recovery disabled; MinGW links the real common VFS/time
and threading dependencies needed by the hash translation unit. MSVC C89 and
MinGW runs pass. These are compatibility and memory-safety tests, not throughput
or gameplay measurements. MD5 tests include known vectors, split/truncated spans,
unaligned full blocks and exact input/output aliasing.

`run_guest_hash_stream_test.sh` checks the C89 MD5/SHA-512 guest adapters
against FFmpeg's actual source with stack contexts. SHA-512's oracle uses the
correct 200-byte state, so this does not preserve the old Final export's wrong
SHA-256 layout. Independent `abc` vectors check the digests. The driver compares
entire guest states and guards across split/unaligned updates, truncated and
aliased outputs, repeated finalization, update after finalization, input/state
aliases and 64-bit count wrap. The reference allocator aborts if called; both
reference and production adapters use stack state. The adapter and driver build
as strict C89; existing FFmpeg source is compiled separately in its native C
dialect. `LARGE_GUEST_HASH=1` adds a greater-than-256 MiB MD5 update with a partial
initial block, checking both digest and retained buffer across length chunks.
Linux ASan/UBSan and MSVC runs pass including this large case; MinGW runs pass
the ordinary suite. These are compatibility/safety checks, not speed benchmarks.

`run_guest_sha1_test.sh` checks the C89 guest SHA-1, HMAC and three-span SHA-1
adapters. The old TinySHA1 class is used only through a minimal historical C++
bridge; FFmpeg's independent SHA-1 source checks all guest-state bytes, guards,
partial blocks, aliases, repeated finalization, count wrapping and padding.
The adapter and driver compile as C89; existing reference code uses its original
C/C++ dialect. RFC 2202 HMAC vectors include a short key, and all key lengths
0 through 160 exercise the streamed API's preserved 64-byte key limit. Short-key
pads now cover the full block, matching the upstream Canary correction.

The common implementation intentionally fixes TinySHA1's truncated bit length
at 512 MiB and above. After building, run the generated `test --large` to stream
513 MiB using only a 1 MiB input allocation: independent SHA-1 agrees, while the
historical finalization yields a different digest. Normal runs also check a
synthetic boundary counter. Host benchmarks and raw measurements are in
`results/sha1-9950x3d/README.md`; `run_sha1_benchmark.sh` reproduces them. Timers
are confined to developer benchmarks.

### XLast metadata

`bash tools/tests/run_xlast_tests.sh` checks the C89 XML/query adapter against
pugixml extracted from Git history, including text encodings, malformed input,
localized metadata and query results. It also injects parser/index allocation
failures and checks cleanup. The Linux default enables ASan/UBSan; use
`SANITIZER=` with the same runner under MSYS2 MinGW. Native MSVC builds of the
C adapter and drivers use `/TC /W4 /WX`.

The production core has no pugixml dependency. `run_xlast_benchmark.sh` measures
construction, destruction and repeated lookups separately; its reference code
also comes from Git history. Benchmark timers are not part of emulation.

### Guest output without standalone presentation

`bash tools/tests/run_presenter_tests.sh` builds a strict C89 driver and a minimal
C++ bridge to the production guest-output mailbox. It checks capture before the
first frame, failed refresh retention, blanking/re-enable, and coherent monotonic
frames across 200,000 concurrent publications using libretro-common threads.
Linux defaults to ASan/UBSan; use `SANITIZER=` under MSYS2 MinGW. The MSVC driver
also compiles with `/TC /W4 /WX`; link the bridge and `src/xenia/ui/presenter.cc`
with the built `retro-common.lib`.

UI-removal validation also covers the complete MSVC core, static MinGW Make
build, audio format/cadence, repeated invalid-content close, APC interruption,
and Vulkan/D3D12 interface/lifetime fixtures. Those lifecycle fixtures do not
boot a retail game or prove visual output across all titles. macOS and Android
SDK builds are not available in this validation environment.

### Bounded pipeline-description records

Run bash tools/tests/run_file_records_tests.sh (SANITIZER= under MSYS2).
The C89 fixture checks a 1 MiB synthetic short-read VFS, a 64 KiB request bound,
read-call counts excluding per-record I/O, exactly one sequential seek,
window reuse, random access, I/O failures without stale-row publication,
output bounds, incomplete tails, empty snapshots and native borrowed mappings.
run_cache_file_tests.sh also checks the production C89 record validator:
unaligned stored hashes, valid/corrupt prefixes, stride/field bounds, sink and
I/O failures, writer/reader sharing, append while mapped, fixed snapshot count,
and successful repair after closing the view. Linux uses ASan/UBSan; MSVC
helper/driver builds use /TC /MD /O2 /W4 /WX.

Both GPU backends now replay from this reader. Their existing nontrivial C++
descriptor types are decoded into one reusable object; raw mapped bytes are
never treated as live C++ objects. This remaining control-record copy is not
claimed to be zero-copy. Queued jobs reference owned pipeline entries, not the
reused descriptor or file window. Retail cache replay/performance still needs
validation; the core lifecycle fixtures use synthetic failed content.

### Mapped file blobs

Run bash tools/tests/run_file_tests.sh (SANITIZER= under MSYS2).
The C89 fixture exercises native mapping, exact-allocation VFS fallback,
short reads, EOF/error cleanup, refusing to reopen an owned blob, repeated
close, empty files and borrowed-data bounds. Linux runs ASan/UBSan; native
MSVC builds the helper and driver with /TC /MD /O2 /W4 /WX. The Vulkan load
owner is retained until vkCreatePipelineCache returns; these component tests
do not validate a retail driver's cache compatibility or title performance.

### Persistent shader-cache VFS

Run bash tools/tests/run_cache_file_tests.sh (SANITIZER= under MSYS2).
The C89 driver checks short reads/writes, EINTR retry, zero writes and disk-full
errors, failed flush/truncate/seek, bounds and overflow, refusing a truncating
retry for a known existing file, native reopen and 4 KiB encoder boundaries.
A minimal C++ bridge calls the production templated shader reader: a valid
1057-word record retains its hash/layout and an impossible 31-bit length in a
tiny file is rejected. Linux runs ASan/UBSan; MSVC uses /TC /MD /O2 /W4 /WX for
the driver/helpers and C++20 for the bridge. This is not a retail shader-cache
replay, write-failure reproduction or multi-process writer test.

### Direct audio production

Run bash tools/tests/run_direct_audio_tests.sh (SANITIZER= under MSYS2).
The C89 converter test checks exact output bits for every span size from 1 to
257 frames, all supported channel layouts, planar big-endian stride, volume
and output bounds. Queue tests exercise original and direct-conversion writes
through 200,000-frame streams, mirrored/plain wraps, known channel-matrix
impulses, the 64 ms bound, readiness and stop while full. Linux uses ASan/UBSan.
MSVC uses /TC /MD /O2 /W4 /WX; compile the converter and queue tests separately
with the selected common sources or built retro-common library. MinGW's runner
relaxes only the existing common rtime.c function-pointer-cast warning.
Android NDK r27d also compiles the NEON converter as strict C89 for ARM64.
These tests do not prove loaded-game audio or native ARM64 execution.

### Bespoke Vulkan texture allocator

run_gpu_spans_tests.sh checks 300,000 operations against an independent byte
occupancy model: alignment, full coverage, coalescing, fixed metadata exhaustion,
64-bit overflow, and stale/duplicate/cross-arena/modified handles.
run_vk_heap_tests.sh uses a stub Vulkan driver to check cold warming, 10,000
warm image/buffer cycles without backing growth, optimal/linear separation,
required dedicated-allocation chains, oversized-block reuse, restricted memory
types, budget refusal and image/buffer bind-failure rollback. Both default to
Linux ASan/UBSan; SANITIZER= is used under MinGW. New helper/driver code builds
as strict C89; Khronos headers are treated as system headers under GCC.

core_vk_heap_device_test.c uses an actual Vulkan loader/device to create and
destroy 3,200 image/buffer pairs with the production allocator. It submits no
GPU commands, creates no window and adds no GPU wait. MSVC uses /TC /MD /O2
/W4 /WX; link the span/heap C files and device test (dl on Linux). These checks
cover host allocation/binding and teardown, not retail drawing, resources in
flight or title-specific RSS/VRAM/performance. Existing core lifecycle fixtures
still exercise Vulkan/D3D12 negotiation and unload separately.

### GPU resource retirement

Both Vulkan and D3D12 use this shared C89 retirement queue. D3D12 retains resource descriptors until their last submitted GPU use completes and clears its CPU binding snapshot before retiring orphan targets. The queue tests cover lifecycle policy; retail rendering and peak VRAM require gameplay validation.

bash tools/tests/run_gpu_retire_tests.sh checks dependency-order destruction,
completed versus in-flight submissions, callbacks freeing their own nodes,
200,000 deferred retirements, final quiescent drain and frame/generation-based
collection cadence including 64-bit overflow. Linux defaults to ASan/UBSan; use
SANITIZER= under MSYS2 MinGW. Native MSVC uses /TC /W4 /WX with
libretro/core_gpu_retire.c and tools/tests/core_gpu_retire_test.c.

The intrusive queue allocates nothing and belongs to the command-processor
thread. Framebuffers precede orphan images in the queue. Existing EDRAM
ownership selection retains all ownership representations, while render
passes remain cached because CPU pipeline jobs have a separate lifetime from
GPU submissions. The threshold triggers collection rather than promising a
hard VRAM bound. The captured retail slowdown used 4x rendering, about
30.2 GiB dedicated plus 2.0 GiB shared GPU memory and failed image allocations;
reproducing visual output and peak memory for that title remains necessary.

### Scheduler pause gate

Run bash tools/tests/run_pause_gate_tests.sh (SANITIZER= under MSYS2).
The C89 driver races six workers against resume, stop and resume-followed-by-stop
for 192 rounds, covering notifications before and during parking and terminal
shutdown. Windows MSVC builds the same driver with /TC /W4 /WX and the built
retro-common.lib. Linux uses ASan/UBSan by default. All workers are joined before
their eventcount gate is freed; tests introduce no polling sleeps.

`run_io_mailbox_tests.sh` exercises the production C89 serial I/O mailbox with six producers and 196,608 messages in eight reusable nodes per producer. It checks FIFO across pending/incoming batches, per-producer order and immediate node reuse, followed by 24,576 messages freed by the consumer while producers continue publishing. Default Linux builds enable ASan/UBSan; native MSVC and MinGW runs are also supported. This verifies handoff ownership, not retail I/O throughput or bounded request admission.

Native audio queue tests also cover complete-batch readiness at the exact 3,072-frame/64 ms limit, one-frame overflow, SIZE_MAX/null/stopped inputs, consumer-driven wakeup and stop cancellation without a periodic writer timeout. These tests exercise queue production and readiness, not loaded-game audio callbacks or Tekken frame pacing.

`run_input_mailbox_tests.sh` exercises the C89 input handoffs: six readers and 200,000 coherent publications, monotonic packets, a paused pinned reader, all 16 snapshot slots pinned, multi-reader keystrokes, 64-event oldest overflow, disconnect clearing, 400/100 ms frontend repeats and atomic rumble pairs. Linux enables ASan/UBSan by default. `run_input_driver_bench.sh` builds the actual old/current C++ adapters behind a minimal C ABI, validates mapping/hotplug/keystrokes/rumble (plus frontend callback reentry in the new adapter), then runs serial forward/reverse read/poll comparisons against `446c545f`. Set `INPUT_DRIVER_VERIFY_ONLY=1 SANITIZER=-fsanitize=address,undefined` for sanitizer interop checks without performance measurements. Synthetic uncontended results and their limits are recorded in `input_mailbox_bench_results.txt`; they do not establish retail FPS or full-system input latency.

Run `run_render_target_collection_tests.cmd` from an MSVC x64 developer prompt after the core build, or run the matching `.sh` against the Make objects. The actual common cache test preserves all four EDRAM ownership forms and warm CPU bindings across 10,000 empty collections, then verifies completion-delayed orphan retirement. It does not measure retail-game frame pacing.

`run_gpu_transfer_tests.cmd` (MSVC C89) and `.sh` (strict C89, Linux ASan/UBSan by default) compare 1,048,576 borrowed ownership-transfer invocations with the previous copied-record model. They check two-pass capacity, sorted order, merge grouping, host-depth suppression without source mutation, stable scratch storage and overflow/reset, plus 245,760 reusable descriptor pairs at the full 30,720-slot limit. The actual common-cache fixture also checks 774,400 transfers across 512 fragmented/wrapped extents using fixed source lists, attaining the 2,048-disjoint-tile bound. Additional cases check coalescing and wrapped ordering, and reject insufficient output capacity before any ownership mutation. These are metadata/lifetime checks; shaders, guest output bytes and retail frame times are not measured by this fixture.

`run_bitmap_tests.sh` exercises the production C89 bitmap with eight common threads and 400,000 exclusive leases. It checks exact forward/backward word order, exhaustion, duplicate/out-of-range release, reset, shrink/regrow, failed allocation rollback, the 32,768-slot trampoline size and zero hot reallocations. Atomic transition cases cover old-value versus new-value branch conditions, signed counter wrap, 32/64-bit CAS and packed-byte preservation. Linux defaults to ASan/UBSan; MinGW uses `SANITIZER=`. `run_bitmap_tests.cmd path-to-retro-common.lib` provides strict MSVC C89 coverage. The implementation is included by the test solely to intercept realloc for failure injection and allocation accounting. These tests do not establish guest hardware-reservation accuracy, complete SList reclamation safety or gameplay performance.

`run_spv_output_tests.sh` compares 2,624 EDRAM helper modules with the archived parent output, covering color/depth, MSAA, scaling, direct resolves, transfer modes and format conversions. The four-byte words must match exactly. It extracts seven unchanged static shader emitters from that archive to avoid unrelated configuration/device dependencies. A second pass checks stable high-water storage. Strict C89 tests exercise instruction wire limits, zero-copy borrowed operand input, reuse, overflow, failed allocation rollback and idempotent cleanup. The full builder bridge compares vector/C89 output including 300 KB source chunks, repeated serialization and failure recovery; optional `SPIRV_VALIDATOR` validates those debug modules. Set `SANITIZER=-fsanitize=address,undefined` for Linux. `BASELINE_ARCHIVE` accepts a pre-exported parent archive for cross-host worktrees. This does not establish retail FPS gains or eliminate remaining IR allocations.

`run_spv_output_tests.cmd` runs the C89 encoder and native MSVC builder bridge from a developer prompt. The metadata regression deliberately supplies a temporary string, then serializes after its destruction; ASan caught the former dangling pointer. Metadata is now encoded into owned C89 storage at submission.

The borrowed-view regression adds eight archived-vector/current-view modules covering nested structures/arrays, dynamic access-chain indices, unrelated mutable frontend state, regular/specialization constants, replicated composites, swizzles and inputs overwritten immediately after consumption. The normal validator checks variants 0–3; variants 4–7 use the replicated-composites extension, which older validators may not recognize, and are checked byte-for-byte. C89 scratch tests run 100,000 reuse/overflow/canary cases. `CAPTURE_ALLOCATIONS=1` optionally counts C++ `new` calls in the isolated EDRAM fixture through the existing C89 profiling helper; it does not count C allocations, driver work or establish timing/FPS gains. The default baseline is `65e648b61`; older vector-output baselines remain supported.
`CAPTURE_ALLOCATIONS=1` statically links the test C++ runtime so allocation and deallocation use the same override, including out-of-line string operations. This affects only the profiling fixture.
The EDRAM matrix also repeats with 32x16 generation scales, exercising a fill band wider than the eight retained bias slots. Both standard and wide variants compare 2,624 modules, for 5,248 byte comparisons total; this is an offline generator stress case, not a new exposed resolution option.
