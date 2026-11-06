# Architecture audit: evidence and remaining work

Reviewed all four pages of the user-supplied architecture audit on 2026-11-06.
The document is an optimization proposal and an architectural summary, not a
verified completion report. User requirements remain authoritative.

| Audit claim | Current evidence and disposition |
| --- | --- |
| Containers are purged and compilation has deterministic timing. | Incomplete. `std::function`, vectors, strings and dynamic allocation remain in the core. Removing a container does not prove deterministic timing. Continue caller/ownership audits, C89 storage migration and allocation measurements. |
| IR pages are fixed, pre-faulted and every rewrite is O(1). | Not established. `core_spv_arena.h` explicitly permits retained-page growth for larger modules. Type/CFG objects remain in the translator. Complexity and first-touch behavior require measurements for each operation. |
| Final words go directly to caller storage. | The C89 writer supports this, but the remaining translator graph and upstream construction costs do not disappear. Preserve wire-level equivalence while continuing the emitter migration. |
| Native DXIL covers the described hardware operations. | Keep the direct DXIL architecture, but a broad prose claim is not a feature-by-feature correctness test. Require shader validation, backend compilation and coverage-specific semantic tests; do not infer GPU correctness from container structure alone. |
| Atomic rings completely eliminate shader stutter and worker parking. | Unsupported. Bounded request/publication/persistence queues remove specific allocations and copies. Idle workers park, full queues apply backpressure, and driver compilation or storage can still delay progress. Never replace necessary idle waits with spinning to satisfy this claim. |
| A 4,098-byte packet provides complete decoder storage. | That is the reusable compressed-frame capacity. FFmpeg trailing padding is additional, and decoded PCM/frame history and guest output rings are separate. It is not end-to-end zero-copy audio. See `AUDIO_OPTIMIZATION_PLAN.md`. |
| Reuse bypasses OS memory boundaries and guarantees flat performance. | Unsupported. Reuse avoids measured allocation requests; paging, caches, scheduling and GPU/driver behavior still matter. Report measured scope, not a hardware-independent guarantee. |
| All high-water allocations are permanently pinned. | Retention is subsystem-specific. Retained capacity is not proof of physical page locking. Audit lifetime, peak committed bytes and teardown reclamation separately. |

## Additions to the active optimization goal

- Produce subsystem budgets for guest-dependent JIT, shader, audio, I/O and GPU
  working sets. Separate reserved virtual space, committed pages, live objects
  and retained reusable capacity; do not treat requested allocation bytes as RSS
  or VRAM measurements.
- Evaluate pre-reservation and deliberate first-touch at safe initialization or
  content-preparation phases. Measure cold growth separately from warmed reuse.
  Preserve frontend context/device ownership and supported platform behavior.
- Exercise pool exhaustion, oversized valid workloads and allocation failure.
  Define explicit capacity/backpressure/failure behavior before imposing a fixed
  arena. Never corrupt memory, silently drop audio or impose an unproven
  game-complexity cap based solely on a historical high-water observation.
- Continue eliminating playback/translation allocations and staging copies by
  constructing directly in reusable destinations and borrowing storage with
  explicit leases. Verify reinitialization, cancellation and content close.
- Evaluate a shared arena as a measured design candidate. A single immutable
  boot-time allocation is not an established implementation or a guarantee of
  driver/OS allocation freedom. Keep the full zero-allocation objective while
  tracking host-owned and driver-owned allocations separately.

Acceptance requires actual allocator/copy coverage, bounded-storage failure
tests, cache/compiler output equivalence, memory-safety tests and representative
latency measurements. Offline microbenchmarks do not establish gameplay frame
pacing, sound quality or the elimination of shader stutter. Release hot paths
must not acquire profiling/logging overhead for these measurements.
