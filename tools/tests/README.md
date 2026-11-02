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