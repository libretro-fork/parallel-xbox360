# Direct SPIR-V emitter migration

The requested end state is removal of `src/xenia/gpu/spirv` and its legacy
builder/IR dependency from the libretro core. Moving that directory or renaming
its classes does not satisfy this requirement. This migration is incomplete.

## Current cuts

- Live readable-order serialization uses a callback pointer and borrowed context
  rather than `std::function`. Exception containment remains at the C++ bridge.
- The unused standalone postprocessor and typed canonical-rewrite helpers are
  removed from source/build entries. Raw-construction tests replace dormant-pass
  tests; actual helper/geometry/guest outputs remain the runtime byte oracle.

- All eleven decoration factories bypass typed instructions and the set
  owner. C89 borrowed keys compare target ID, opcode, operand flags and words
  before allocating; unique records encode directly into module-owned payloads.
  Output borrows raw words. The dormant standalone postprocessor and its
  pruning/alignment/alias APIs are removed after a whole-runtime caller audit.
  Records retain only an eight-byte ID-interval prefix plus wire words.
  Metadata/payload growth is checked.

- Uniform composite constants borrow one scalar ID through a C89 logical source.
  Repeated hashing, exact-key matching and packet emission need no intermediary
  member array. Normal views and repeated sources share constant identity.
  Scalar smears and composite comparison/extraction remove temporary vectors.
- Required extensions use sorted C89 pointer metadata over module-owned encoded
  words. Lookup and output borrow the encoding; disable/re-add reactivates the
  same blob without allocation. Unique names stay cached until lease reset.
  Metadata/payload storage can still grow at new high-water marks.
- Obsolete operand-only storage and its compatibility API are deleted. Packet
  growth/free/string code now belongs to the C89 packet implementation itself.
- Module ID registration, packet lookup, type queries and checked pointer-storage
  queries use the C89 packet index. These borrow existing records; no duplicate
  index, type cache, packet buffer or ownership transfer is introduced.
- Every production instruction now uses a C89 encoded packet. Its type/result
  prefix and operands share a wire-word span; output borrows that span rather
  than encoding fields again. ID flags and operand patches remain available.
  Typed ownership/CFG wrappers still remain, and final module assembly still
  copies packets into Vulkan's required contiguous module.
- The Vulkan version probe emits its final 39 words in C89 caller storage. It
  constructs no builder, types, functions, blocks, instructions or output vector.
- Capability membership uses retained sorted C89 values. Capability and memory
  model output write final words directly, without temporary Instruction objects.
- The quad specialization helper borrows a fixed four-ID array rather than
  allocating a vector adapter.
- Existing C89 caches, pointer groups, stable frame pages, CFG flags/plans and
  final binary ownership are reusable components for the replacement backend.

These cuts do not remove the guest translator's legacy instruction graph.

## Remaining runtime dependency

`SpirvBuilder` derives from the legacy builder; guest translation, EDRAM helpers
and geometry generation still construct typed instruction/function/block IR.
That graph supplies type/constant queries, branch targets, Phi/Switch fixups,
reachable structured block order. Core generators already emit required
features and block-local sampled images; no runtime caller used the deleted
standalone postprocessor.
Removing the remaining instruction/function/block graph before replacing its
live duties would break shader generation.

The enum headers describe SPIR-V and its extended instructions. In particular,
`GLSL.std.450` is a SPIR-V extended-instruction set, not a GLSL source compiler.
The replacement must retain required opcode information without a language parser.

## Replacement work still required

1. Extend the C89 encoded packets into an owned module emitter with stable
   encoded-word spans and ID/type indexes. Packets already write their wire
   prefix/operands during construction; replace the remaining typed graph/index
   interfaces with C handles and queries over those words.
2. Replace block/function objects with stable C89 handles and encoded block
   spans. Preserve outer-loop references or migrate all their callers before
   changing that interface. Forward labels, Phi inputs and Switch operands need
   checked offset fixups whose backing storage cannot move underneath borrowers.
3. Preserve explicit feature selection by the core generators. Capabilities,
   decorations and extensions already use C89 owned records during emission.
   Preserve block-local sampled-image construction; the unused standalone late
   inference/cloning passes have been removed and must not be reintroduced.
4. Finalize logical module sections and reachable structured block order with
   one final ownership handoff. Vulkan requires contiguous module words: do not
   claim zero copies by hiding a section-flattening buffer. Count and measure
   any unavoidable final assembly, and avoid encoding the module twice.
5. Migrate guest translation and all EDRAM/geometry helpers to those C APIs, then
   remove the legacy sources, adapters, unneeded debug/language helpers and build
   entries. Keep only the required protocol definitions as owned C data.

## Evidence required at each backend cut

- Native MSVC C89, strict C89 and ASan/UBSan ownership/failure checks.
- Old/new shader bytes where identity is preserved; explicit validator and
  semantic coverage for intentional representation/ID changes.
- Existing helper/geometry matrices, structured-control-flow/sampled-image
  cases and actual cached Tekken/Tomb Raider translations. These caches alone
  do not establish all-game correctness or runtime GPU performance.
- Construction/serialization allocation, requested/retained memory and timing
  measurements, with cold reservations and remaining growth disclosed.
- Working audio, renderer resource retirement and shutdown ordering preserved.

The directory remains until these duties are replaced and verified. The broader
optimization goal remains active throughout this migration.
