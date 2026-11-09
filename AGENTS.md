# Rules for coding agents

These apply to any AI agent working in this repository (Claude Code reads them through `CLAUDE.md`). They came out of testing the libretro cores with their testers.

## Top priority: eliminate draw-path compilation and compilation waits

Correct rendering without first-use compilation stalls is a top development priority, alongside crashes and memory corruption. Fix the remaining Vulkan and D3D12 violations before treating narrower shader micro-optimizations as a solution to cold-play stutter. These are requirements to implement and verify, not descriptions of compliance already achieved.

- After initialization, draw submission, the GPU command processor, guest CPU execution and `retro_run` must never perform shader translation/emission, shader-module creation, driver compilation, graphics/compute PSO creation or program linking. Follow the complete call graph, including callbacks and utility passes. Creating a supposedly cheap placeholder, satisfying a first-seen key, hitting a driver cache, disabling async, losing workers or exhausting retirement storage does not waive this rule. Cover generated geometry/tessellation, EDRAM transfer/resolve, texture conversion and shared helper pipelines too.
- Required generic/fallback pipelines must be prepared before gameplay. Specialized pipelines may be built by workers while a ready implementation executes the same guest semantics. Preserve shader stages, texture access, pixel kills, depth/stencil/blend behavior, memexport, attachment ownership and query results. No-op pixel shaders, missing draws, stale frames and approximate output do not qualify as correct fallbacks.
- Frame-critical execution must not wait for compilation, pipeline publication, queue capacity or a compilation-queue drain. Dispatching work to another thread and immediately awaiting it still violates the latency requirement. Normal guest-required GPU synchronization is separate; identify its real dependency and do not hide compilation waits behind it.
- Compilation admission must be bounded and nonblocking for the submitting draw. Under pressure, defer optional specialization and continue with the ready correct pipeline. Never run a compilation job inline to make room. Idle workers may park using libretro-common primitives. Keep cache-table mutation on its owning thread, publish immutable worker results with checked key/generation identity, and preserve both CPU-consumer and GPU-submission lifetimes through cancellation, reset and unload.
- A cache miss or an unseen state must still render correctly without waiting for specialization. If the ready accurate fallback is missing, record that path as an unresolved architectural failure. Do not conceal it by dropping work, switching to a misleading async label or reporting procedural compile-time comparisons as gameplay acceptance.

### Known violations to remove

The audit at `9ebb681b0` confirms these defects; documentation and prior component-test passes do not resolve them:

- `VulkanPipelineCache::ConfigurePipeline` creates placeholder PSOs immediately and has a synchronous real-pipeline branch. `CanCreatePipelineAsync` falls back when no pixel shader, worker, placeholder module or retirement reservation is available.
- `PipelineCache::ConfigurePipeline` in D3D12 also creates placeholders inline and selects synchronous creation when async conditions fail. Both draw paths may translate shaders or wait for a requested specialized pipeline.
- The shared creation queue can block admission for reusable storage. Current stand-in paths include no-op pixel shaders and skipped draws; neither establishes accurate fallback coverage.

Track the replacement and missing instrumentation in `libretro/SHADER_COMPILATION_REGRESSION_PLAN.md`. Policy/evidence updates may document this debt; do not mark the renderer compliant or the cold-play goal complete until the runtime gates and correct fallback coverage pass.

## Branches and history

- The `libretro` branch is never deleted, and nothing that rewrites its history is run on it: no force-push, no rebase, no amending or squashing of commits that are already pushed. It is the fork's only long-lived branch.
- New features and experimental changes go on their own branch, and testers test builds of that branch. When testing is done, the related commits are squashed and merged into `libretro`, so the main branch does not collect commits that were superseded midway.
- A fix that users of the `libretro` builds need before the branch is merged is cherry-picked into `libretro`, in a way that does not break the later merge of the branch.

## Merging upstream Xenia Edge

This repository builds the libretro core and nothing else. The standalone's parts - its UI, packaging, upstream's CI and its contributor docs - were deleted on purpose, and must not come back with a merge. The paths are listed in `.upstream-excluded`.

- Upstream is has207/xenia-edge, branch `edge` (remote `edge`). Only full merges of it, no cherry-picked upstream commits.
- Right after `git merge` of upstream, before resolving anything else, run:
  `git rm -r -q --ignore-unmatch --pathspec-from-file=.upstream-excluded`
  This settles the modify/delete conflicts in favour of the deletion, and it also removes files upstream newly added under those paths, which git would otherwise bring in without any conflict.
- Never resolve a conflict on one of those paths by restoring the file.
- When upstream changes a build file around one of the removed parts, keep them removed and take the rest of the change.
- When something new is deleted for the same reason, add its path to `.upstream-excluded` in the same commit.
- After a merge, `upstream.version` names the upstream version merged; the core reports that version, and a new release is built.
- `CHANGELOG.md` is this fork's own. When a merge conflicts on it, keep this one.

## These files

- Only the developer writes to or deletes `AGENTS.md` and `CLAUDE.md`. An agent does not change them on its own; it proposes the change to the developer instead.

## Changelog and README

- `CHANGELOG.md` is updated in the same commit as the change it describes: what changed for someone using the core, newest first. Not a copy of the commit message.
- A change on a feature branch gets its entry on that branch, under "Unreleased"; it reaches `libretro` with the squash merge.
- `README.md` is kept true in the same way: when a change makes a sentence in it wrong (a limit lifted, an option added, a file moved), fix the sentence in that commit.

## Code

- Keep comments true. When a change makes a comment describe behaviour that no longer exists, fix or remove the comment in the same commit.
- Look at the big picture, not just the function being changed. For example, resetting the content and closing it have to release the game's resources through the same path; a reset is an unload and a load.
- Stop and join threads the way upstream Xenia Edge does in its own shutdown, not with methods made up for the libretro port. Ad-hoc ones are what cause shutdown and reset bugs.
- Every core option has to be connected to something. Do not add an option the core does not read, and remove one that turns out to do nothing. Option defaults follow the standalone's defaults unless there is a reason, written down beside the option.
- Xenia's own config file is not where the core's settings live. If you need a setting, use a core option (the `.opt` file) and set the cvar from it.

## Testing

- Obtain the frontend log and exact core build/commit before diagnosing a report. The core no longer writes a separate xenia.log; do not ask testers for one or restore a logging worker/file to obtain benchmark diagnostics.
- Compare with standalone Xenia Edge at the same upstream version before calling something a core bug; when standalone fails the same way, it is upstream's.
- Each Windows CI build uploads its `.pdb` as `<artifact>-symbols`; resolve a reported `parallel_xbox360_libretro.dll+0x...` crash offset against symbols from that exact core build.

## Cold-play shader and PSO regression contract

- First-ever play with no emulator shader or pipeline cache must approach warmed-cache fluidity. Obvious shader hitches, multi-second pauses, missing draws, incorrect fallback rendering or disturbed audio cadence are defects. A faster isolated translator or smaller SPIR-V file does not establish this outcome.
- Every commit is compared with its first parent using the same versioned corpus, harness, build options, device/driver and cache condition. A push containing several commits must test each commit, not only the final tip. No path filter excludes documentation or build commits from the comparison workflow. Keep a separately pinned last-known-good reference once sustained cross-game testing establishes one; never automatically promote the latest result or hide accumulated regressions behind a moving parent.
- Run correctness before timing: shader/PSO static-state identity and dynamic-state separation, vertex/pixel/geometry/tessellation where supported, control flow, texture/format paths, queue saturation, duplicate publication, cancellation, failed compilation and unload/reset lifetime. A missing device, skipped suite, empty corpus, incompatible baseline, timeout or missing metric is not a pass. State exact unsupported coverage.
- Measure guest analysis/translation, generated bytes/instruction counts, module creation, driver graphics/compute PSO creation/linking, queue admission/waits, first correct draw, frame intervals and cache I/O separately. Retain per-case raw samples, sample counts, median/p95/p99/max, outlier frame IDs, environment and cache manifests. Never substitute aggregate average FPS, total process startup or byte-count savings for compile-stall evidence.
- Compare matched cache conditions: fresh process with private empty emulator caches and controlled/identified driver caches; recipes present with driver binaries absent; warm persistent caches; and warm in-process objects. Empty VkPipelineCache alone does not prove an empty driver disk/internal cache. Isolate copies of saves/system directories; never delete or flush the user's live caches or replace their installed core to obtain a benchmark.
- Include several game families and heavy vertex/pixel shaders, not just DS2: at least NG2, DS2, Tekken Tag Tournament 2 and another known-correct title in private authorized replay coverage. Public CI uses redistributable procedural fixtures; software Vulkan/WARP results do not establish hardware-driver or gameplay smoothness. Do not publish extracted game shaders or assets as CI artifacts.
- Use alternating parent/candidate and reverse-order repeats on an otherwise idle runner. Pin the toolchain/driver identity and compare each case and tail, not just a corpus mean. Predeclare relative and absolute noise thresholds. A repeatable regression must be fixed or selectively reverted before a performance change lands; inconclusive/noisy or unavailable timing remains unverified. Keep the existing regression debt visible while infrastructure is introduced; an already-bad parent is not a smoothness acceptance baseline.
- The top-priority draw-path contract above has no synchronous-compilation or compile-wait exception for queries, memexport, queue pressure or missing fallback coverage. Preserve those guest dependencies through a ready accurate implementation; document any remaining violation explicitly.
- Add test-only event-site accounting for translation, module/program/PSO creation, compile waits, blocking admission and inline queue execution, classified by thread role and pipeline kind. Post-initialization frame-critical counts must be zero. Tests must exercise first-seen states, disabled async, worker loss, queue/retirement exhaustion and fallback coverage, plus reset/unload and savestate restore in a fresh process where serialization is supported. Unsupported restore coverage remains unverified. Existing timing CSVs are not these counters. Compile instrumentation out of release builds; no per-event formatting, logging or wall-clock sampling in production.
- Compare cold and warmed frame-time tails at equal correct images, submitted draws and actual guest progress. Repeated presentation of an old image cannot manufacture a faster result. Associate stalls with event counters before blaming compilation; keep image/readback validation separate from timing runs. Track initialization-to-first-correct-frame and generic-pipeline preparation costs, then `retro_run` p50/p99/max and missed budgets during play.
- Preserve a pinned smooth-play reference and explicit regression debt. A local improvement must not silently reset a worse accumulated baseline. Only lower accepted regression limits with equivalent coverage and measured evidence; changing tolerances or promoting a reference needs explicit user authorization. Keep driver/backend/device identities and all failed or noisy samples.
- Reduce exact-key permutations and duplicate jobs before adding workers. Hash hits require exact identity. Canonicalize only state actually emitted dynamically on the enabled device path; include shader options, backend/target, host configuration, attachments/layout and cache schema in compatibility. Never use a pointer as persistent shader identity; normalize padding and compare complete canonical bytes. An invalid/incompatible cache is a recoverable miss. Version cache changes explicitly and measure the newly cold path.
- Zero-copy, borrowed views, reusable storage and VFS/data-transfer work are part of the PSO pipeline. Record payload bytes read/copied/uploaded and allocations separately. Do not slurp a cache/archive merely to forward or parse it, do not byte-loop copies, and do not rewrap/copy immutable shader payloads at handoff. Use bounded reads or borrowed mapped views where supported, direct final destinations and ownership transfer. Keep mappings, immutable snapshots and Vulkan/D3D12 resources alive through their last CPU/GPU consumer; document unavoidable copies and non-mapping fallbacks.
- Cache lookup/discovery must not synchronously read/write files on frame-critical threads. Use bounded worker-side libretro-common VFS/data-transfer or validated mapped views with explicit ownership; optional cache I/O backpressure must not block a draw or cause unbounded retained memory. Current cache defaults remain unchanged without measured evidence and explicit user direction.
- Investigate warmed GPU execution regressions as well as compiler time: reproduce the workload, compare emitted code and actual GPU cost at matching accuracy/resolution, and retain a numerical/image regression fixture. Static instruction/word counts alone prove neither faster compilation nor faster rendering.
- Keep diagnostics opt-in/test-only and production paths MSVC C89-shaped with narrow typed interoperability. Do not add per-shader release logging, synchronous readbacks, GPU/CPU roundtrips, sleeps or frame pacing to make a benchmark pass. Preserve libretro video/audio delivery and actual guest progress.
- Follow `libretro/SHADER_COMPILATION_REGRESSION_PLAN.md`. Publish CI artifacts and explicit coverage limitations with shader/PSO changes. CI infrastructure and reproducible failure capture can land while recording the existing cold-play failure; they do not certify that the regression is fixed. Changing cache defaults or loosening performance thresholds requires explicit user direction and measured evidence.

## Libretro pitfalls already hit in the other cores

Each of these was a bug in at least one of the cemu, rpcs3, vita3k or xenia cores. Check new code against them before asking testers.

- Audio: each retro_run hands the frontend exactly one frame's worth (sample rate / declared fps), never "whatever is queued". The game's audio thread waits once about 64 ms is queued; no bigger buffer on the core's side. Several streams (audio ports, clients, a music player) are mixed, not appended one after another, and each is taken in its own format and sample rate.
- Geometry: the size the frame is handed over at goes to the frontend: base geometry at load, SET_GEOMETRY when it changes, SET_SYSTEM_AV_INFO when it would exceed the max. The max has to cover the highest internal resolution.
- Pacing: the guest's vblank follows retro_run, and the core declares the guest's refresh rate; the frontend paces display and audio, the core does not sleep to pace itself.
- Files: everything the core writes goes under system/<core>/ (or saves/); nothing next to the RetroArch executable. Warnings and errors use the frontend log callback; there is no separate core log file.
- No window: the standalone's UI (on-screen keyboard, notifications, message boxes, profile dialogs) has no window in a core. Every path that reaches for the window or its UI thread needs a headless branch that answers the way the user most likely would.
- Unload: never end the process (no exit, no TerminateTitle-style shutdown). Stop and join every thread in upstream's order, and never wait without a timeout on a fence, event or thread the frontend has to drive. No vkQueueWaitIdle/vkDeviceWaitIdle while RetroArch holds its queue lock.
- Libraries that pin themselves (statically linked OpenSSL) keep the core loaded after dlclose; build them so they do not.
- No downloads at run time; data files ship with the core.
