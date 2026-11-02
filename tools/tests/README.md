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
direct resolves and ownership-transfer shaders still need native emitters.
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
ownership transfer remains to be migrated.
`make check-dxil-pixel-interface DXIL_VALIDATOR=/path/to/dxv` checks float and
integer color outputs, unpacked depth/stencil outputs, and per-sample input
metadata needed by ownership transfers. All five validate and pass WARP pipeline
creation. For that pipeline check, place a compatible native VS from the geometry
fixture at `<pixel-path>.vs` and run `dxil_geometry_runtime_test.exe <pixel-path>
<mode> pixel`. This checks pipeline creation, not pixel execution.