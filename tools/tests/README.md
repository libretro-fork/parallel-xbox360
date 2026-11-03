# Raw DXIL writer checks

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

These checks validate the writer foundation. The guest shader translator and
the remaining D3D12 rendering paths still need to be connected to it before
the old conversion backend can be removed.

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
Texture fetches, live resource bindings, stage export handling and D3D12
pipeline integration remain required before it can replace the core's guest
shader backend.

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
they do not implement real vertex/texture access. Stage resource bindings,
fetch conversion, exports and runtime pipeline integration remain unfinished.
The frontend is not yet used by the live renderer.
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

Fixture memory is a shader-local array. Live shared-memory resource binding,
texture fetches, stage exports and renderer integration are still required.
No live renderer path uses this frontend yet.
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
WARP. These are development fixtures; the live core still uses its previous
shader backend. Texture fetches, stage assembly/exports and live renderer
integration remain unfinished.
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
this fixture is 2D. Guest coordinate/face conversion, half-texel offsets,
signed/gamma treatment, sampler policies, texture state and register writes
remain to be implemented. No live renderer path uses these primitives yet.
## Windows GNU compiler and ABI checks

The following fixtures are authored in MSVC-compatible C89. They can also be
compiled with MinGW-w64 GCC using `-std=c89 -Wall -Wextra -Werror`.

- `core_events_test.c` checks coalesced title-stop requests and stale-state
  clearing. Run `make check-core-events` on Windows or Linux.
- `context_switch_test.c` checks two fiber resumes using the owned
  `make_x86_64_ms_pe_gas.asm` and `jump_x86_64_ms_pe_gas.asm` objects under
  `third_party/boost_context/context/src/asm/`. Assemble with GCC using
  `-x assembler-with-cpp -DBOOST_CONTEXT_EXPORT=`; link the objects to the test.
- `d3d12_struct_return_test.c` checks five D3D12 C ABI adapters against real WARP
  objects. Compile it with `src/xenia/ui/d3d12/d3d12_struct_return.c`, add
  `-Isrc -isystem . -isystem third_party/DirectX-Headers/include`, and link
  `-ld3d12 -ldxgi -ldxguid -lole32 -luuid`. The C adapter object can also be
  linked to the fixture compiled as C++ to check calls from the renderer.
- `core_load_test.c` loads a DLL, checks the 25 required libretro exports,
  API version and core name, then unloads it. Compile with `-isystem libretro`.
  Run `core_load_test.exe ./parallel_xbox360_libretro.dll`, then repeat with
  `lifecycle` as the second argument for two init/deinit cycles before unload.

The Windows GCC build passed all these checks, including the context-switch
and D3D12 adapter tests compiled with MSVC. These are ABI and lifecycle smoke
checks; they do not exercise game execution or an actual title exit.

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
Memory-export allocation, pixel kill handling and texture policy still require
stage callbacks; their renderer paths continue using Mesa.

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
the same vertex bytes to accept native and remaining Mesa vertex fixtures.

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
hardware register while preserving metadata name/declaration order. Mixed
native/Mesa VS/PS and Mesa VS/native GS checks cover independent compilation,
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
(configs 6/7), and verifies unsupported sample/border/LOD queries and vertex
derivatives fail native generation so the renderer can retain its fallback.
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

Current renderer sampling integration covers 1D, 2D, 3D/stacked and cube at unit draw resolution.
Scaled sampling, memory export and tessellation still require
native implementation; these tests do not establish game compatibility.

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
instruction remains unimplemented as in the existing Vulkan path; it now bypasses
Mesa without inventing different output. Explicit-gradient or non-pixel computed
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
