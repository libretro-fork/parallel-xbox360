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

## Immediate priority: enforce the draw-path contract

The top-priority rules in [AGENTS.md](../AGENTS.md) govern this work. The audit at
`9ebb681b0` found inline placeholder and synchronous real-PSO creation in both
backends, draw-side translation, compile/publication waits, blocking queue
admission and inaccurate stand-ins. Fixing these is a priority, not a completed
optimization. The historical NG2 capture above remains separate evidence.

1. Add test-only event-site counters by thread role and pipeline kind; cover
   graphics/compute, helper pipelines, shader emission/module creation, queue
   admission and waits. Exercise disabled async, missing workers, queue saturation
   and retirement exhaustion. Current component CI does not certify these zeros.
2. Establish complete accurate generic-pipeline coverage before gameplay and move
   specialization entirely to workers. Preserve query/memexport/attachment
   dependencies. Remove inline placeholder creation and all synchronous branches;
   replace no-op pixel shaders/draw skipping rather than presenting them as fixes.
3. Make specialization admission nonblocking and preserve key/generation identity
   and CPU/GPU lifetime. Workers may park; a draw must use its ready correct path
   without waiting for compilation or free job storage.
4. Compare first play with warmed play at identical images and useful guest work,
   including frame-time tails, boot/preparation cost and fresh-process savestate
   restore where supported. Keep validation readbacks out of timing runs. Missing
   coverage stays visible; isolated PSO metrics cannot certify gameplay smoothness.

This policy update changes no production code and introduces no runtime counters
or fallback implementation. Existing first-use defects remain open. Retain current
cache defaults, automated per-commit CI and the pinned-reference policy.

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

### Draw dependency wait containment

Vulkan IssueDraw and D3D12's two pipeline-by-handle await paths now wait on the
requested publication's pending bit. The C89 worker pool reuses its existing
slot-release eventcount, so no per-pipeline event, polling loop, allocation or
release-path notification is added. Callers retain the pool and publication;
a queued creator clears pending before releasing its lease. Cache shutdown
continues to join creators before destroying publication storage.

Ordered publication is retained: an earlier ordered job may still hold up the
requested pipeline. Later jobs and independent unordered warmups do not need
to finish. Startup and strict submission-wide drains remain. A 128-round fixture
covers successful and failed target publication with three unrelated leases
held; substituting the old whole-queue wait fails its bounded negative control.
The CI textual alarm counts zero-argument queue drains separately from target
waits; it is still not a dynamic counter. See
`tools/tests/pipeline_target_wait_results.txt` for component comparisons.
This contains one stall amplifier, not the target compiler's own latency or a
semantically complete fallback. Full cold-play acceptance remains outstanding.

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

## Independent RTZ/FTZ regression profile (corpus contract 2)

Hardware float32 rounding and denormal capabilities are independent. Query them
separately: [Vulkan float-control properties](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFloatControlsProperties.html).
The tested RTX 5090 supports RTZ and signed-zero/NaN preservation, but not FTZ.
Requiring both previously selected the large integer arithmetic fallback. Native
RTZ plus signed input/output flushing preserves arithmetic with much smaller IR.
No cache schema or default changes; guest microcode caches remain usable and
changed module words naturally select different driver programs.

Keep the original 64 procedural translation cases unchanged. Hardware comparison
adds a separate 64-case RTZ/manual-FTZ profile, 16 module and 16 graphics-PSO metrics
(contract 2: 192 metrics total, versus software Vulkan 96 and WARP 80). Before timing, execute
192 real packed-guest ADD/MUL/FRC cases covering both lowering paths, mixed lanes,
denormals, cancellation, finite overflow and nonfinite operands. Finite results
are bit-exact; NaNs permit payload canonicalization only. Missing float-control
or transform-feedback support fails explicitly rather than counting as coverage.
Hosted builds generate the fixtures; an opted-in hardware runner executes them.

Seven alternating parent/candidate pairs pass current thresholds. The ordinary
64 modules remain byte-identical; the new profile shrinks 12,489,152 to 1,729,472
bytes. Its worst observed NVIDIA PSO drops from 364.8671 to 135.919 ms. This
remains a visible-stall risk, not a smoothness signoff. Driver disk/internal cache
coldness is unverified and no sustained multi-game cold-play anchor is certified.
Detailed evidence and remaining limits: tools/tests/shader_hw_rtz_ftz_results.txt.

## Ordered dot products (corpus contract 3)

The independent per-lane products may share one native-RTZ vector instruction.
Keep the scalar addition order, per-lane Shader Model 3 zero/NaN correction and
NoContraction. Do not substitute OpDot or a reassociated reduction. Integer
fallback and pixel policies remain unchanged. Operands stay borrowed compiler
IDs; no new payload buffer, queue, lock or arena is introduced.

Preserve both original 64-case profiles. Add a separate 64-case dot profile with
DP4/DP3/DP2ADD, uniform inputs, differing constants and 32/96/256-op chains across
the same VS/PS and swizzle/resolution profiles. Hardware comparison now requires
288 metrics. Software Vulkan 96 and WARP 80 are unchanged. Expand actual-guest
GPU correctness to 384 cases; the original 192 are retained, followed by 192
dot cases. An explicit 1.0 + three 0.75-ULP case rejects reordered sums. A private
production negative control which reverses lane order fails at the expected
finite output; restored production and exact parent pass.

Seven alternating pairs show no threshold alarms; worst observed dot PSO is
380.8468 -> 293.7932 ms, not stutter-free. Existing two SPIR-V profiles remain
byte-identical, as do eight DXIL cases. Driver disk/internal cache coldness and
sustained cross-game cold play remain unverified. Native hardware-FTZ dot
execution is not supported by the tested GPU; it is not claimed as physical
coverage. See tools/tests/shader_native_dot_results.txt for full scope.

## Multi-profile harness validation and outstanding hosted alert

Every timed Vulkan PSO process emits one device/driver report. Require exactly
one report for software Vulkan and three for the current hardware original,
RTZ and dot profiles, with identical device/driver identity both within and
across samples. Do not mistake additional valid profiles for a device change.
Missing or duplicate reports and mixed identities remain hard failures.

The hosted software-Vulkan comparison for e3bc8472e flagged case12 p95
102.5708 ->131.7618 ms (maximum103.4080 ->133.7248 ms). All64 generated modules
are byte-identical. This timed PSO harness does not call the changed core worker
queue. The alert's cause remains unproven; retain the failed samples and repeat
under the same thresholds, rather than dismissing it or loosening the gate.
The first failure is evidence even if a later run passes. Current local
hardware/WARP self-checks validate orchestration only, not this hosted alert
or a cold-play acceptance baseline.

## Reusing established denormal state without tracking storage

Inside a dot reduction, both the corrected product and the preceding sum have
already passed output FTZ. A narrow two-bit operand mask permits the typed ALU
adapter to reuse that fact; no global value-state map or IR-copy pass is needed.
Retain output flushing and reduction order. DP2ADD's third guest operand is raw
and must still be flushed. A negative control which falsely marks it clean
returns zero instead of minimum-normal at real-GPU case197 and is rejected.

Keep cache states separate in reports. The first standard-corpus comparison
produced three PSO tail alarms against previously compiled parent modules;
retain that failed comparison. A separate already-seen-module comparison passes
288 hardware and80 WARP metrics. An independent first-use companion, using the
same procedural source with33/97/257 dot operations instead of32/96/256, gives
96 metrics with no alarms and377.0181 ->211.2981ms worst observed PSO. All companion
parent hashes differ from the previously used corpus. This supports a cache-state
explanation; it does not certify driver disk/internal cache coldness.
Do not replace the standard corpus, delete its first samples or loosen thresholds
to obtain a pass. The remaining211ms stall is still a cold-play defect.
Evidence and exact scope: tools/tests/shader_clean_dot_results.txt.

## Timing contract 2: elapsed gates with supplementary process CPU

Retain monotonic elapsed timings as the authoritative median/p95/p99/max gates.
Add a separate per-process user+kernel CPU channel with the same stage/case and
paired sample indices. It is diagnostic, never a replacement duration or an
automatic reason to pass an elapsed failure. Require complete matching roles,
cases and sample counts; missing CPU coverage also fails. Preserve raw channels.

Before building or timing, compile/run an MSVC C89-shaped clock fixture. Seven controlled
blocking reads must retain >=100ms elapsed while consuming <50ms process CPU;
a separate busy phase must charge >=40ms CPU. The controlled120ms wait belongs
only to offline test orchestration. Check that an injected elapsed stall still
fails. Linux uses CLOCK_PROCESS_CPUTIME_ID; Windows uses process user+kernel
accounting. Parallel workers may exceed wall time and short Windows intervals
may quantize to zero. CPU/wall separation alone does not prove preemption
rather than API waits, driver services or other work.

Record bounded-read hashes/sizes of timed executables and exact module manifests.
Report whether the Vulkan driver executable and module manifest are identical
across revisions; identity does not erase a timing alarm. Store timer controls
inside uploaded results. Attempt-scoped artifact names preserve failed samples
when a workflow is rerun, instead of overwriting or colliding with them.

Hosted3c4f2cd14 software-Vulkan case10 flagged p95 19.6986 ->29.2567ms and
maximum19.7005 ->33.4587ms; median18.9081 ->18.8463ms. All64 modules are identical.
This is a different case from the earlier e3bc8472e case12 alert. Both original
failed reports remain evidence; neither local self-checks nor the new diagnostics
retroactively resolve them. See tools/tests/shader_cpu_diagnostics_results.txt.

## Share native dot-product flush predicates (2026-11-09)

The C89 emitter may lend the already-required zero-exponent predicate alongside
a flushed value. Native-RTZ/manual-FTZ dot products reuse those IDs to select
positive zero when either required operand is zero/subnormal, replacing repeated
scalar absolute-value/minimum/classification operations. Preserve lane-identity
masks, ordered scalar sums, output FTZ and the raw DP2ADD third operand check.
Do not turn the existing flush API into a different recipe for unrelated paths.

Keep timing corpus3 and timing contract2 unchanged. Increase hardware numeric
coverage to576 real packed-guest cases, preserving the original384 and adding
192 same-register yxzw cases with differing xy and identical zw lanes. Classified
C89 flush tests cover65536 patterns, signs/exponents and callback failures. A
private negative control using AND instead of OR for zero predicates must fail
zero-times-NaN/Inf output; the restored path must pass.

Keep three conditions visible. The first standard comparison again has three
PSO tail alarms when previously compiled parent modules meet new candidate
modules. Retain those raw samples. A separate already-seen comparison passes
hardware288 and WARP80 metrics. A novel companion using34/98/258 dot operations
instead of32/96/256 gives96 metrics without alarms: worst observed PSO
303.4499 ->194.3020ms, with paired process CPU296.875 ->187.500ms. This supports
reduced compiler work, without certifying empty driver disk/internal caches or
gameplay smoothness. The standard dot corpus shrinks8,510,048 ->7,213,152 bytes.
Do not describe the194ms remaining stall as invisible. Full details, including
unchanged outputs and unsupported coverage: tools/tests/shader_sm3_predicate_results.txt.

## Independent result publication (2026-11-09)

A request's compilation completion must make its own entry usable even when an
older request remains in progress. Queue admission order is not a dependency
between independent pipeline entries. Remove the ordered result window, lend the
original bounded request slot through Store, then destroy and release that slot.
Keep backend command order, GPU completion gates and placeholder retirement.
Only the command owner advances submissions; workers acquire the shared atomic
submission counter when transferring replaced handles to retirement.

The C89 fault-injection driver must block the first creator while a later entry
publishes success or either backend's failure policy. AwaitPending must return
for that entry while the first job remains outstanding. Fill the 4096-slot
capacity, release the held lease, reuse the entire ring, and verify exact-once
stores. Shutdown must join a held creator before releasing entry owners.
The same driver must reject the exact ordered parent within its bounded timeout,
releasing its artificial block before teardown. Keep move-only/copy-budget,
cancellation, stale token, resize, MPSC retirement and real backend publication
coverage. No driver compilation, rendering or guest audio timing claim follows
from this scheduling test. Shader corpus and elapsed regression gates remain
unchanged. Results: tools/tests/pipeline_ready_results.txt.

## Lane-wise native fractional parts, corpus4 (2026-11-09)

For native-RTZ vertex fractional parts, flush the borrowed input once, emit a
lane-wise Floor, then an uncontracted subtraction with both operands known clean.
Floor yields zero, integral normal values or non-finite values, never subnormals.
Keep output flushing. Preserve the scalar integer fallback and pixel lowering.
No new tracking storage, payload copy, worker or cache schema is needed.

Retain all corpus3 cases and thresholds. Corpus4 adds64 fraction-heavy VS/PS
modules across native/portable swizzle and1x/4x, producing96 additional hardware
translation/module/PSO metrics (384 total). Software Vulkan96 and WARP80 remain
unchanged. Run the same current harness against each revision, including its
first parent; seal the extra modules and require four device reports per hardware
sample instead of three. Reject missing/duplicate reports and substituted cases.
Native structural tests require one Floor with the expected width1..4. Retain
all576 physical-GPU numeric cases; their existing fraction cases check signed
zeros, subnormals, NaN/Inf and RTZ boundaries against the independent oracle.

The first comparison has no alarms across384 hardware and80 WARP metrics.
Fraction vertex bytes8,336,768 ->1,528,960; first heavy PSO548.2137 ->276.7256ms,
with paired process CPU468.750 ->265.625ms. Disk/internal driver caches are not
certified empty. This remaining277ms stall is visible and not a cold-play pass.
Do not infer GPU execution speed or gameplay fluidity from shader size alone.
Detailed evidence and limits: tools/tests/shader_fraction_results.txt.

## Instruction-local operand lends (2026-11-09)

Borrow an already-loaded raw register/float-constant ID only inside the same
scalar/vector ALU operation, before guest state writes. Match source kind, full
index and addressing mode exactly. Apply swizzles and sign/absolute modifiers
independently afterward. Do not add a cross-instruction value cache, mutable
alias map or assumed scalar/vector pairing order. Unknown sources/modes retain
the original load/validation path. Use the bounded C89 matcher with typed field
projection, two fixed keys and no payload-copy/ownership transfer.

Require adversarial/randomized key checks, native VS/PS duplicate-versus-distinct
load counts and the existing576 GPU goldens (including mixed same-register
swizzles). Changing the matcher to ignore addressing mode must fail. Keep corpus4
and elapsed thresholds unchanged. Retain all three conditions: the first standard
comparison flags44 PSO tails against previously seen parent modules; a novel
RGB-export/33,97,257 companion passes384 metrics, and separate already-seen
standard comparison passes384 hardware/80 WARP metrics. Do not overwrite the
first failure or describe this as faster first-use driver compilation. Matched
first-use costs remain essentially unchanged, including316ms required PSOs.
Details and unsupported coverage: tools/tests/shader_operand_lend_results.txt.

## Vector software RTZ and shared result types (2026-11-09)

The exact integer add/subtract/multiply recipe is lane-independent. For Vulkan
without native RTZ, lend whole vector IDs through the existing C89 recipe rather
than emitting it per component and reconstructing the result. Preserve integer
wrap/shift/CLZ behavior, finite/non-finite selection and signed FTZ. Use native
32x32 extended multiply without shader Int64/Float64. Reuse simple undecorated
product result types by exact member types in the current module; never key only
by a name or reuse IDs across modules/widths. Pixel and hardware-RTZ paths stay
unchanged. No new buffer, tracking map, copy, thread, lock or readback is needed.

Require one recipe for widths1..4, repeated scalar products sharing a result
type, distinct scalar/vector member types and mixed-width use in one module.
Keep all576 real GPU goldens and make a valid high/low product-word fault fail.
Keep corpus4 and elapsed gates unchanged. Preserve the initial standard run's
eight PSO tail alarms against already-seen parent modules. A novel matched
XYW-export/34,98,258 companion passes384 metrics and lowers worst observed
PSO341.4789 ->269.3778ms. This remains a visible stall, not cold-play signoff.
This fallback improvement is not a claim about the unchanged hardware-RTZ path.
Complete cache-condition details: tools/tests/shader_vector_rtz_results.txt.

## Native-RTZ MAD product state, corpus5 (2026-11-09)

A vector MAD product has already passed output FTZ, including any per-component
SM3 zero replacement/shuffle. Lend that clean-state fact only to the add's first
operand. The independent third operand remains raw and must retain its input
flush. Preserve separate uncontracted multiply/add and output FTZ. Do not infer
clean state from a reused raw operand ID, skip the third flush or use FMA.

Keep all corpus4 inputs and thresholds. Corpus5 adds64 MAD-heavy modules/96
hardware metrics, making480 hardware metrics and five device reports per sample;
software Vulkan96/WARP80 remain unchanged. Retain original576 numeric programs,
inputs and goldens byte-for-byte, then append128 independent-third MAD programs
(native/software, ordinary/partial same-register swizzles). New fixtures use48
input bytes; legacy576 remain32. Pack c0/c0/c2 as c0,c2 according to the shader's
constant map; validate exact file lengths. A valid SPIR-V fault bypassing only
the third flush must return zero instead of signed min-normal and fail the GPU
oracle. Do not weaken expected values to accommodate a packing mistake.

The first comparison passes480 hardware and80 WARP metrics. MAD32 vertex bytes
3,944,192 ->3,528,448; worst first PSO592.7058 ->437.2861ms with paired process
CPU593.750 ->437.500ms. Existing four profiles and all non-targeted numeric
modules remain unchanged. Driver disk/internal coldness is not certified;
a437ms required compile is still a visible defect. Detailed coverage and limits:
tools/tests/shader_mad_results.txt.


## Software-RTZ fraction vectors, corpus6 (2026-11-09)

Lend the complete flushed input vector to Floor and the existing software-RTZ
subtract recipe. Every operation is lane-wise. Preserve raw input/output FTZ,
nonfinite behavior and unsigned integer shift semantics; do not enable FMA,
lower precision or change the hardware-RTZ and pixel paths.

Keep every corpus5 case and threshold. Add64 software-fraction-heavy modules
(native/portable swizzle,1x/4x,32/96/256 chains),96 metrics on the physical Vulkan
runner. Require576 hardware-runner metrics and six device reports per sample;
software Vulkan96 and WARP80 remain unchanged. The physical runner executes the
software RTZ shader path for this profile even though its device supports RTZ.
Keep all704 numerical fixtures and goldens; test widths1..4 and reject the old
scalar implementation with the structural guard.

Seven alternating process pairs trigger no existing timing gate. Generated32
software fraction VS modules shrink27,146,624 ->7,074,432 bytes and translation
median0.98360 ->0.43970ms. First heavy PSO689.4333 ->726.7886ms is worse within
the unchanged tolerance; report it, never substitute byte savings for driver
stutter improvement. All320 previous profile modules,32 new pixel modules and
8 DXIL modules are identical. Driver disk/internal coldness and gameplay remain
unverified. Full evidence/limits: tools/tests/shader_software_frac_results.txt.


## Native product zero-predicate lends (2026-11-09)

For vertex native RTZ with manual FTZ and fully distinct multiplicand lanes,
reuse the existing classified-flush product helper from dot lowering. Preserve
output FTZ, NoContraction and MAD third-input flushing. Partial identity masks,
software RTZ, pixel and hardware FTZ retain their previous paths. Test widths1..4
and all64 product/mode variants, retaining a failing pre-change structural control.
No corpus6 cases or thresholds are removed/loosened.

The first comparison flags three MAD PSOs: previously compiled parent binaries
create in under1ms while changed candidates incur57.8/146.4/366.0ms first-use
work. Preserve those failures. A separate seven-pair warm comparison passes all
576 physical Vulkan/80WARP metrics. Additional unseen33/97/257-chain workloads
built from the same private C89 fixture against the exact parent/candidate pass
96 metrics, with first heavy PSO510.9311 ->369.6393ms. Both observations are
required context; never present the warm rerun as the original first result or
call an unknown driver disk/internal cache empty. Keep the extra fixture source,
exact-parent rebuild/restoration logs, executable hashes and every raw sample.

All704 numerical inputs/goldens are identical; only32 ordinary native MUL and32
ordinary native MAD modules change. Existing320 non-MAD performance modules,
32 MAD pixels and8 DXIL binaries are byte-identical. Details and remaining
cold-play limits: tools/tests/shader_product_predicates_results.txt.


## Partial native product predicates, corpus7 (2026-11-09)

Reuse the existing per-lane masked SM3 product helper for partial aliases too.
Project differing source lanes into compact result order with the C89 lane-mask
helper. Preserve identical-lane products, signed FTZ, NoContraction and the MAD
third input's flush. Never apply an uncompacted source mask to a smaller result
vector. Exhaust all256 masks against explicit source-order tables, including
ignored high bits, and structurally test every nonempty output mask.

Keep every corpus6 case and threshold. Corpus7 adds64 partial-MAD VS/PS compile
cases (r0.xyzw*r0.yxzw+cN,32/96/256 chains,swizzle/scale configurations). Require
672 physical-runner metrics and seven matching device reports per sample.
Software Vulkan96,WARP80 and numerical704 remain unchanged. Verify the new
profile actually reaches partial correction: parent shader000 has32 NMin/64FAbs,
candidate has neither. The parent must fail the new partial structural guard.

Seven alternating pairs pass on the first full comparison with no alarms.
Partial32 VS bytes3,944,192 ->3,314,688 and first heavy PSO442.2871 ->360.2223ms.
Translation median0.39815 ->0.40540ms is essentially flat/slightly higher; record
it. Existing384 performance modules,32 new pixels,8DXIL and all input/golden
files are unchanged; only32 partial native MAD numerical modules differ.
Driver disk/internal coldness and gameplay are unverified. Full details:
tools/tests/shader_partial_product_results.txt.


## Software-RTZ dot coverage, corpus8 (2026-11-09)

Add64 software-RTZ DP4/DP3/DP2ADD-heavy VS/PS programs,32/96/256 chains across
existing swizzle/scale configurations. Force software RTZ even on hardware with
native RTZ. Require768 physical-runner metrics and eight matching device reports
per sample; software Vulkan96,WARP80,numerical704 remain unchanged. Keep all
corpus7 cases and all thresholds. Case identity/missing/duplicate-report checks
must cover the new stages. Confirm that the baseline emits128/288/512 extended
product recipes in shaders0/1/2, covering scalar4/3/2-lane products.

Do not infer compile-time improvement from smaller SPIR-V. A rejected vector
product experiment reduced32 VS bytes23,315,584 ->18,574,832 but heavy first PSO
2219.2048 ->2630.7755ms, with three failed timing cases. Keep every raw sample
and rejected-source identity; do not ship that runtime lowering or hide its
failure with warmed reruns. Ordered sums were correct and704 numerical cases
passed, which did not establish acceptable performance.

The delivered commit changes test infrastructure only. All512 parent/candidate
performance modules,704 numerical programs/inputs/goldens and8DXIL binaries are
identical after restoring production sources. Strict C89/MSVC fixtures,128 new
module validations and27 comparator tests pass. A new local timing comparison
was blocked before launch because RetroArch PID29120 was running; leave that
process untouched and report timing unverified. Infrastructure/failure capture
may land under this plan's existing allowance; no runtime performance pass is
claimed. See tools/tests/shader_software_dot_results.txt and hosted CI artifacts.

## Draw-event instrumentation foundation (2026-11-09)

The opt-in stats profile now accounts at translation and19 direct driver
creation sites, plus pool waiting/caller execution and current bad stand-in
paths. Component/actual-translator negative controls fail the zero-event gate.
Separate full-core profile builds validate the bounded snapshot ABI and release
export isolation. This closes instrumentation plumbing only: no first-correct-
draw gameplay replay, accurate generic fallback, worker-loss/retirement-pressure
renderer integration, or restore coverage is established. Queue `TryPush` is
nonblocking but not yet adopted by renderer callers. Do not exchange a blocked
job for an omitted draw. Existing inline creation and waits remain failures.
Keep the original performance/correctness gates and defaults unchanged.

## Quiet analysis (2026-11-10)

Guest analysis now skips diagnostic disassembly formatting and its retained
string copy unless explicitly requested.42 full/quiet metadata and emitted-byte
fixtures,704 GPU numerical cases and384 sampler cases pass. Seven unchanged-
threshold pairs show no regression and identical shader bytes; per-case median
analysis/translation ratios are about0.887. This removes avoidable cold-path CPU
work, not driver compilation or waits. No loaded-game/cold-frame acceptance or
accurate ready fallback is claimed. See tools/tests/shader_analysis_text_results.txt.

## Ready Vulkan layout ownership (2026-11-10)

Remove the draw/compiler layout mutex, two mutable maps and shared binding-input
vector. Fully built native owners publish into an immutable C89 cache; workers
retain exclusive high-water scratch. Hits do not wait behind native creation.
Losing creators destroy duplicate handles, and joined teardown preserves pipeline-
before-descriptor destruction. A missing full layout now fails specialization
before PSO creation. Ownership/race/input/device checks and unchanged shader timing
gates pass; see tools/tests/layout_ready_results.txt. This removes one blocking
edge, not shader compilation or the need for an accurate ready fallback. Native
creation, allocation and driver synchronization on misses remain unverified costs.


## Ready D3D12 geometry ownership (2026-11-10)

Native geometry cache hits borrow immutable DXIL without the previous mutex/map.
The C89 owner preserves completed failures, releases unpublished losers and
retains published binaries through joined creators and pipeline teardown. Optional
dumps forward borrowed bytes through VFS without word-length truncation. Ownership,
real-emitter byte identity and WARP geometry execution are checked separately from
procedural shader timing. This does not move geometry emission off the draw path,
remove cold allocation, change vertex/pixel binding locks or implement accurate
nonblocking fallback rendering. Those remain architectural failures to fix.
See tools/tests/geometry_ready_results.txt for exact coverage and timing limits.
