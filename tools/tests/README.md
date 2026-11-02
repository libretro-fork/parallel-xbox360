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
Whole-shader control flow, fetch/binding lowering, export handling and D3D12
pipeline integration remain required before it can replace the core's guest
shader backend.
