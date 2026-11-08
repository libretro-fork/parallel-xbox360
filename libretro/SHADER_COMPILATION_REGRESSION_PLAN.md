# Cold-play shader and PSO regression plan

## Outcome and current failure

First-ever play, with no emulator shader/pipeline cache, must approach the fluidity
of a fully warmed run. Compilation must not visibly interrupt correct gameplay.
Cache reuse remains useful, but must not be the mechanism that hides a broken
first-use path. Keep current cache defaults until measured evidence and explicit
user direction justify a change.

This is not achieved at the current implementation baseline `c4b8ef972`.
The captured NG2 log identifies that build and reports zero shaders loaded from
storage. It contains 83 draw-side pipeline waits totalling 22,857.08 ms; 66 waits
associated with logged frame 2153 total 21,442.37 ms. The longest single logged
wait is 1,700.25 ms. These are elapsed queue-wait durations, **not isolated
driver-compiler timings**. The largest reported vertex SPIR-V is 1,065,944 bytes.
No matched historical NG2 shader-size/driver-latency comparison has yet proved
which commit caused the regression. Preserve that distinction.

The recent DS2 CPU translation improvements, smaller static shader metrics and
reduced scratch allocations do not establish smooth cold play across games.
Correctness fixes, byte-identical storage changes and code-generation changes
must be evaluated separately rather than indiscriminately reverting all work.

## Inputs and decisions

The user's current instructions govern this plan. The supplied PDFs are strategy
and evidence, not commands to execute without verification:

- `parallel_cores_implementation_plan.pdf`: consider pre-discovery, ready fallback,
  bounded asynchronous compilation and reusable memory. Its proposed signature
  scanner does not prove complete shader discovery in compressed/encrypted or
  runtime-generated assets. Never slurp an ISO to scan it. A compute ubershader
  is not automatically a correct substitute for the full Xenos graphics pipeline.
- `parallel-xbox-shader-pso-handoff.pdf`: adopt its distinctions between module
  words, live modules, observed full PSO recipes and opaque driver caches; exact
  identity, semantic dynamic siblings, bounded jobs, ownership and cold/warm
  methodology. Its Xbox/NV2A/GL implementation is not evidence that Xenos paths
  or this core already support those mechanisms.
- `carmack_speed_of_light_optimization.pdf`: account for necessary work and actual
  bytes moved, not only performance relative to an inefficient predecessor.
  Preserve emulation semantics and lifetime; mutating a buffer still in use is
  corruption, not an optimization. Architectural limits do not waive measurement.
- `parallel-xbox360-shader-renderer-optimization-handover-2026-11-08.pdf`: retain
  the checked shader correctness, publication and allocation evidence while
  explicitly correcting the missing cold-driver/gameplay acceptance coverage.
- [parallel-gx1 AGENTS draft](https://github.com/libretro/parallel-gx1/blob/master/AGENTS.md):
  aligns on ready correct fallback, no draw-path compilation/waits, bounded
  workers, ownership, separate work/timing evidence and a ratchet. Here the user
  additionally requires automated per-commit comparisons. Do not import its cache
  default or assume its renderer-specific coverage exists in this core.

Do not use nonexistent generic "Vulkan pipeline fences" as a completion protocol:
ordinary pipeline creation completes through the host API call; a worker publishes
the ready object afterward. Optional deferred-host-operation APIs have their own
support and lifetime rules. GPU submission fences/timelines retire resources
used by commands; they are a separate lifetime from CPU compilation completion.

## Phase 0 - identify and contain the regression

1. Freeze a reproducible scene, image/title-update identity, input/guest-work
   sequence, current build and candidate earlier revisions. Start with NG2, DS2,
   Tekken Tag Tournament 2 and another known-correct title, privately. Run both
   backends and native/high internal resolution where supported. Do not publish
   extracted game assets; public CI inputs are procedural and redistributable.
2. Preserve the actual cache manifests. Distinguish emulator recipes, translated
   modules and driver blobs. Diagnose why the captured NG2 run loaded zero shaders;
   do not automatically attribute this to a version bump. `.xsh` guest-shader
   storage is not an arbitrary SPIR-V template file and must not be overwritten
   with a different representation under the same schema.
3. Compare exact guest programs/modifications across revisions. Measure analysis,
   translation/serialization, module creation, driver PSO creation, waits and first
   correct draw separately. Record generated words/instructions/control-flow
   structure and shader-family attribution. A smaller module can compile slower.
4. Audit draw-side placeholder PSO creation, queue-wide drain in IssueDraw and
   query handling, per-shader modification serialization and duplicate-claim
   waits. Record the correctness reason for each wait and which job can satisfy it.
5. Selectively fix/revert a measured regression. Retain regression fixtures and
   compare all title families. Do not delete valid control-flow fixes or blanket
   re-enable incomplete shader behavior to get a faster result.

Acceptance: matched raw measurements identify the source of each significant
stall; the candidate improves cold-play tails against the pinned working reference
without changing pictures, guest progress, audio cadence or cache semantics.
Current component CI is not this acceptance test.

## Phase 1 - per-commit CI and honest baseline accounting

### Implemented component workflow

`.github/workflows/shader-compilation.yml` runs on every branch push, including
documentation/build commits, and on explicit dispatch. It enumerates newly
reachable pushed commits, including merge-side commits, and compares each with its
own first parent. New-branch events use their pushed commit list where supplied.
Unsupported/oversized ranges fail explicitly rather than silently testing the tip.
No PR-target job executes untrusted contributions on a hardware runner.

The triggering revision's harness is fixed across each pair. Production sources
and ABI headers come from separate exact-revision worktrees. This avoids comparing
different test algorithms or linking an old object against a new translator layout.
An incompatible historical harness/API is a failed/unverified comparison, not a pass.

Hosted jobs build GNU/Linux with a separate software Vulkan driver and native
MSVC/Windows with WARP. The software Vulkan driver is a **test dependency only**;
Mesa is not restored to the core. Builds use at most half the reported CPU count,
capped at five jobs; source builds and timing phases are serialized within a job.

Current measured coverage:

- 64 procedural production VS/PS translation cases: nested bool/predicate calls,
  ALU chains of 32/96/256 additions, native/portable swizzle profiles, 1x/4x.
  Each timed interval performs four fresh analyses/translations using a retained
  translator, then borrows the resulting bytes. It excludes dump I/O and does
  not measure boot or translator construction. Profiles without texture fetches
  do not prove texture-format coverage.
- Vulkan: actual module calls and 16 VS+PS graphics PSOs spanning eight programs
  and two static topology/blend recipes. The explicit pipeline cache is null.
  Separate existing sixteen-case vertex transform-feedback goldens execute on
  the selected driver before the timed runs. PSO acceptance is not full rendered
  image validation of the ALU stress shaders.
- D3D12 WARP: actual native DXIL vertex generation and eight graphics PSOs using
  the existing clip/cull/point/interpolator configuration fixtures, with their
  independent numerical checks. This is not all native DXIL stages or PSO states.
- Strict raw-data/schema checks, per-case output-size growth alarms, toolchain
  and driver identity records, and source-text alarms for additional queue-drain
  or placeholder-creation references. Text alarms are **not dynamic draw-thread
  compile/wait counters** and can neither prove zero waits nor detect all new paths.

Seven alternating parent/candidate rounds run by default; an explicitly pinned
anchor is also rebuilt/measured on the same runner. Each raw record is retained.
Reports include per-case sample counts, median/p95/p99/max and module manifests.
Harness revision/file hashes and the corpus contract are recorded too. Preserve
existing case semantics/IDs when extending coverage. A changed corpus needs an
explicit version/review; never shorten its guest workload to improve a score or
silently replace the established reference set.
These are seven-sample component quantiles, not confidence estimates for gameplay
tails. No corpus mean can hide an individual failing case.

Initial declared alarms (all in seconds; both relative and absolute conditions
must be exceeded): median +10% and +1 ms for translation batches, or +2 ms for
module/PSO calls; p95 +25% and +5 ms; maximum +50% and +10 ms. A maximum alarm may
mean noise: it still fails for investigation, never auto-passes. Output growth
above both 10% and 4096 bytes is a per-case review failure. Recalibrate thresholds
from retained repeated controls; do not increase them merely to make a change pass.
Missing cases, nonfinite/negative times, empty files, timeouts, unsupported devices,
wrong drivers and failed correctness are failures. The comparator has negative
tests so a large tail cannot disappear behind an unchanged median.

CI summaries explicitly say that software component success is **not cold-play
signoff**. A green software job does not claim that the known gameplay regression
has been fixed. Infrastructure/failure-capture commits may land with this debt
visible; optimization commits still need the acceptance evidence in AGENTS.md.

### Hardware and non-drifting reference

Register an isolated Linux x64 runner with label `shader-perf` and the required
build tools, Vulkan driver and SPIR-V validator. Set repository variable
`SHADER_GPU_RUNNER_ENABLED=true` only after it exists. No runner is installed or
enabled automatically on the user's computer. Hardware jobs are serialized and
use a read-only GitHub token. The repository had no registered self-hosted runners
when this plan was prepared; absent coverage is displayed in the workflow summary.

Set `SHADER_PERF_ANCHOR` to the full immutable SHA of a user-accepted, sustained
cross-game last-known-good build once it is identified. The workflow compares
against that anchor as well as the parent, including IR growth. Never update it
automatically. If unset, reports explicitly state that no ratchet was certified.
Do not bless the currently regressed build merely to initialize a green baseline.

### Still required for the full gate

Add opt-in/test-build instrumentation at production boundaries: critical-thread
compile/wait counts by reason, exact PSO key, jobs/queue occupancy, first correct
draw/frame IDs, core allocations, payload/cache I/O bytes and final retirement.
Compile it out of release code; do not add unconditional logging or GPU readbacks.
Build stable private replay tooling and expand redistributable fixtures to texture
formats/swizzles, depth/stencil/MSAA, render-pass/layout compatibility, geometry,
tessellation, EDRAM operations, cache rejection, key mutation and unload/reset.
Wire those measured counters and replays into the hardware acceptance job.

Full acceptance requires zero unexpected draw-path compiles/waits, correct output
and unchanged guest progress, plus cold-versus-warm p99/max within measured spread.
This is a target and a required follow-up, not a claim made by the component jobs.
Maintain per-case exception records tied to exact revisions and explicit user
approval; never silently loosen or regenerate a baseline.

## Phase 2 - make unknown first draws correct and nonblocking

Prepare a semantically complete fallback family before gameplay. Inventory its
coverage across Xenos stages, texture/sign/filter behavior, derivatives, discard,
depth/stencil/blending, memexport, EDRAM and queries. Existing interpreter VS plus
no-op/depth-only PS or skipped geometry is not a full correct fallback.

Jobs carry an exact observed PSO recipe and stable references: target/features,
shader modifications, attachments/render-pass compatibility, static/dynamic state,
layouts and descriptor expectations. Deduplicate with exact equality after hash
lookup. Admission is bounded by undrained input/output capacity, not only worker
activity. On saturation defer speculative specialization and keep the correct
ready fallback; never make the draw wait for a free queue slot.

Publish initialized results with release/acquire ordering; adopt on the cache
owner and retire losing duplicates safely. Wait only for the actual dependency
where unavoidable correctness work remains, not an unrelated global queue.
Preserve guest-visible query/memexport results and resource ordering. Stop new
producers, cancel pending work, finish active calls and drain results before
device teardown. GPU users retain native objects through submission completion.

Acceptance: first-use and full-queue adversarial tests show correct drawing while
specialization is outstanding; no critical-thread compiler call, lost wakeup,
stale key, dropped publication, UAF or frontend-driven teardown deadlock.

## Phase 3 - reduce compilation work and permutations

Use exact stage/module and full-PSO identities for their different purposes.
Canonicalize a field only after tracing every consumer and verifying that the
enabled backend actually emits it dynamically. Prove semantic equivalence before
moving specialization into uniforms. Include target/features/configuration,
render-pass and layout identity where consumed; zero hashed padding. Every
same-size semantic schema change needs a deliberate cache compatibility decision.

Pre-discover legal shader programs from validated executable/archive records using
bounded VFS reads, not unchecked signatures or a whole-ISO scan. Runtime-generated
programs remain valid misses. Shader discovery alone cannot enumerate all PSO
recipes: use validated observed full-state recipes, bounded feeding and reuse.
Keep portable guest inputs/recipes distinct from device/driver-specific blobs.
Measure prewarm and boot-to-first-correct-frame cost; shifting a ten-second hitch
to loading is not the same as eliminating the work.

Use the compile corpus to locate IR expansion and expensive driver optimization.
Compare CPU time, driver time and runtime GPU cost before retaining a transform.
Prefer typed direct emitters, local folding, reduced live widths and bounded
reusable builder storage without introducing a GLSL/parser intermediate.

## Phase 4 - integrate zero-copy and file I/O with the same measurements

Keep one immutable compiled payload; transfer its owner or lease spans rather than
creating wrapper copies. Build final output directly where lifetime and wire
format permit. Count necessary IR serialization separately from redundant full
binary copies. Driver-owned internal work is not application zero-copy evidence.

For caches, archives and recipes, validate ranges/versions before lending bounded
views. Use libretro-common VFS/filestream/data-transfer and mapping support where
available. VFS does not inherently return borrowed bytes: use checked chunked reads
into final reusable storage when a mapping is unavailable. Handle short reads,
truncation, partial writes, corrupt records and cancellation; never trust file
size to suppress changed-content writes. Document unavoidable decompression,
byte-order or ownership copies. No byte-loop copy or whole-file slurp simply to
forward the data to another parser/driver call.

Keep mapping/view references until consumers finish; never lend mutable guest
memory across an asynchronous compiler without a stable snapshot or proven
mutation protocol. Stable worker slots must own nested create-info pointer
targets and native object references until host creation completes. Apply the
same rules to Vulkan and D3D12.

Move cache persistence off the frame-critical path into bounded I/O work. Batch
validated dirty records and use checked positioning/repair; do not synchronize
every draw to disk. Zero-queue-space and I/O failure must not invalidate live
render resources. Budget staging, pending bytes and immutable references; do not
replace a small copy with unbounded retained memory.

Acceptance: equal workload and correct output with measured lower payload moves,
allocations or blocking I/O; frame-time evidence remains a separate gate.

## Reproduction protocol

- Use private system/save/cache directories and distinct test binaries. Never
  erase live RetroArch caches or overwrite an installed core to manufacture cold.
- Separate application-cache empty, recipes-only/driver-cache empty, persistent
  warm, warm in-process, and fresh-process savestate cases. Record the actual
  driver-cache policy. A null VkPipelineCache is not proof of an empty driver's
  internal/disk cache. Software CI explicitly disables Mesa's disk cache; hardware
  jobs currently report that driver-cache coldness is not certified.
- Capture cache identities, toolchain flags, OS/CPU/GPU/driver, image/update/scene,
  input trace, renderer/features, resolution and frontend pacing. Use paired and
  reverse-order repeats on an idle runner, with no competing builds.
- Time clean binaries without screenshots, frame hashes, synchronous readbacks or
  GPU-query polling. Validate pictures in separate runs. Retired GPU timestamps
  are not automatically pure GPU time if CPU submission gaps lie inside them.
- Do not skip startup/first-use frames as "warmup" in cold-play reports. Component
  warmed microbenchmarks must say what they exclude. Count consecutive audio,
  distinct correct pictures and guest progress, not just short retro_run calls.
- Retain raw data and frame IDs. Explain a hitch with correlated events; opcode
  counts, GPU utilization screenshots and guesses are not timing attribution.

## Scope of the reference APIs

- [Vulkan pipeline specification](https://docs.vulkan.org/spec/latest/chapters/pipelines.html)
  and [pipeline cache guide](https://docs.vulkan.org/guide/latest/pipeline_cache.html):
  pipeline creation/compilation, explicit cache behavior and capability-gated
  compile-required control. Check enabled features before using optional flags.
- [Microsoft WARP guide](https://learn.microsoft.com/windows/win32/direct3darticles/directx-warp)
  and [D3D12 graphics PSO creation](https://learn.microsoft.com/en-us/windows/desktop/api/d3d12/nf-d3d12-id3d12device-creategraphicspipelinestate):
  WARP is a software adapter and does not stand in for vendor-driver timings.

CI orchestration uses the runner's Python standard library. Core build/runtime
dependencies remain unchanged; production algorithms, fixture generation and
timers remain MSVC-compatible C89 with narrow typed C++ interoperability. No Mesa,
glslang, SPIRV-Cross, desktop LLVM/Clang or Python dependency is reintroduced into
the libretro core.
