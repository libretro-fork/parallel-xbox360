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