# parallel_xbox360 libretro core

A [libretro](https://www.libretro.com/) core of [Xenia Edge](https://github.com/has207/xenia-edge), the Xbox 360 emulator, for RetroArch and other libretro frontends.

This repository follows xenia-edge and adds the libretro frontend in `libretro/` plus owned shader and build paths. It holds the core only; upstream's standalone UI, packaging and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is merged to (`upstream.version`). The core is currently on xenia-edge commit 8dc77b0 of 7 October 2026.

Guest output refresh borrows its synchronous callback and context across both Vulkan and D3D12 instead of copying owning `std::function` objects. Vulkan's shared-device target selection also borrows its frame context. A C89 three-slot mailbox uses libretro-common atomics and nonblocking exclusive consumer leases instead of a consumer mutex; a concurrent capture that cannot acquire a lease returns without waiting, allowing the frontend to repeat its last frame. Slot snapshots remain protected and GPU resource retention/completion ordering is unchanged. This removes callback ownership copies, not the required GPU blit or all presentation allocations. Offline tests cover concurrent producer/consumers, failed refresh, blanking, nested capture and lease moves. No gameplay pacing improvement is claimed.

The supplied architecture audit is reconciled with code evidence in [the architecture audit review](libretro/ARCHITECTURE_AUDIT_REVIEW.md). Fixed-budget and pool-exhaustion work remains part of the active optimization goal; the core is not yet allocation-free or free of shader stutter.

The Vulkan version probe writes its final SPIR-V words directly in C89 caller storage, bypassing the instruction graph and output vector. Capability metadata uses retained sorted C89 storage; capability and memory-model serialization avoids temporary instruction objects. Guest/helper instructions now construct C89 wire packets and output borrows their encoded spans, preserving operand fixups and ID flags. Final module assembly still copies packets into Vulkan's contiguous buffer. The guest translator still uses the legacy builder in `src/xenia/gpu/spirv`; its removal requires the backend migration described in [the emitter migration plan](libretro/SPIRV_EMITTER_MIGRATION.md).

Shader module ID/type queries borrow the C89 packet index directly. The obsolete operand-only storage API is removed; packet growth retains its pooled storage and checked allocation rollback. Pointer-storage queries reject missing or malformed records. The legacy builder's remaining module/type/CFG duties still need migration.

Uniform shader composites borrow one scalar ID rather than staging repeated members in a vector. C89 source hashing/matching/emission preserves ordinary constant aliases and specialization identity; composite extraction borrows scalar operands/indices. Extension names now encode once into module-owned C89 payload storage. Sorted pointer metadata borrows those words for lookup and output; duplicates and disable/re-add cycles allocate nothing. The cold registry reserves 16 pointers, grows for additional unique names, and caches removed names until module reset. Final contiguous SPIR-V assembly still copies packet words. Measurements and lifetime coverage are in `tools/tests/spv_extension_bench_results.txt`.

Decoration generation now bypasses the typed instruction graph and set nodes. C89 keys borrow caller operands and text while checking identity; unique records encode once into module-owned words. Output borrows those spans. The unused standalone postprocessor and its decoration pruning/query APIs are removed. Core generators already emit their required features and block-local sampled images. Decoration records now have an eight-byte identity prefix, and output emits every stored record without an active-flag branch. The cold registry reserves 128 pointers and grows with checked high-water capacity. Final module assembly still copies words; other graph/container allocations remain. See `tools/tests/spv_decoration_bench_results.txt`.

Shader CFG output calls a function pointer with borrowed context, removing `std::function` from the SPIR-V builder/walk. The synchronous bridge still propagates callback exceptions after returning from the C89 walk. Other core subsystems retain `std::function` and are being audited for removal. See `tools/tests/spv_core_walk_bench_results.txt` for measured coverage and limits.

Memory-export shader generation now uses 34 direct loops over a C89 scalar mask iterator, removing its two nested `std::function` helpers and their callable copies. Scaled pixel memory-export shaders declare fragment coordinates even when they write no color target, fixing an invalid-input/null-record translation path. The actual microcode matrix and allocation/timing results are documented in `tools/tests/spv_memexport_bench_results.txt`; callback conversion elsewhere in the core remains unfinished.

## Savestate work in progress

Audio/XMA pause requests use acknowledged common gates with bounded waits. Content teardown releases the decoder contexts and their retained packet buffers after joining the decoder worker.

Frontend savestate callbacks remain disabled. Native tests cover bounded state I/O, memory allocation metadata, six guest CPUs restored into fresh libco continuations, and acknowledged worker/timer pauses. Direct KeDelayExecutionThread checkpoints preserve the remaining delay, including a second restore before first dispatch. Other in-flight HLE calls, nested HLE callbacks and active APCs are rejected. Other kernel waits, GPU contents, complete audio decoder history and whole-machine validation still need implementation before states can be used for gameplay.

## Downloads

Frontend warning/error forwarding uses reusable fixed storage, including for fragmented and long log lines; it does not allocate strings per line.

The buildbot configuration covers Windows, Linux (x86_64, arm64) and Android (arm64-v8a, x86_64). The [Actions workflow](https://github.com/libretro-fork/parallel-xbox360/actions/workflows/libretro.yml) also includes macOS on Apple Silicon. Successful runs publish build artifacts when Actions is enabled.

- **Windows:** `parallel_xbox360_libretro.dll`, whose MSVC builds need the Visual C++ 2015-2022 runtime (GNU Make/MinGW builds link their compiler runtimes statically).
- **Linux:** `parallel_xbox360_libretro.so`, glibc 2.35 or newer; uses the C/C++ runtime, with no X11 or Wayland libraries (the CMake build also links lz4 where available). ARM64 uses the a64 CPU backend.
- **macOS:** `parallel_xbox360_libretro.dylib`, Apple Silicon only. The x64 CPU backend needs the address space below 4 GB, which a core loaded into RetroArch cannot free, so there is no Intel build.
- **Android:** `parallel_xbox360_libretro_android.so`.

## Video

The surface-reuse path keeps compatible GPU images and descriptors when guest surfaces move between EDRAM addresses. Selection uses allocation-free C89 policy and borrowed resource references; cache rekeying retains the existing node. Vulkan framebuffer keys follow stable image identities. Retirement metadata is fixed at construction, GPU completion precedes destruction, and ordinary completion processing shares a per-frame work allowance. Shutdown drains all retired resources. This is an owned implementation informed by upstream's target-reuse fix, not a merge to a newer upstream revision; a tester reported VRAM stabilizing around 9-11 GB over 12 minutes of Tekken Tag Tournament 2 gameplay. Remaining allocation churn and frame-time fluctuations are not considered resolved.

Resolve/dump metadata uses two fixed C89 worklists per backend, allocated and touched at renderer initialization. Each has 2,048 slots: the wrapped physical EDRAM extents are disjoint, and every emitted rectangle contains at least one tile. Sorted invocation records borrow those immovable rectangles until command encoding finishes, avoiding the second metadata copy. The lists reserve about 80 KiB per active 64-bit renderer, do not grow during gameplay, and reset by changing counts rather than clearing payload bytes. Transfer lists and GPU readback/upload growth are not covered by this change.

Ownership-transfer and clear lists use fixed C89 storage throughout the common cache and both backends. Each of five common and two backend source lists holds at most 2,048 disjoint physical transfer ranges. These expand to at most 4,096 color/depth-plus-stencil invocations. Each 16-byte sorted invocation borrows its source until command encoding finishes, instead of copying a 32-byte record; stencil views suppress optional host depth without source mutation. Initialization reserves and touches 400 KiB of source/invocation payload per active 64-bit backend. D3D12 replaces its two descriptor vectors as well: up to five destinations, 2,048 transfer ranges each and three SRVs per range bound scratch to 30,720 CPU/GPU handle slots (720 KiB). GPU handle pairs are constructed once; later requests overwrite live reusable objects without resize/zero-initialization. These thirteen removed vectors do not remove the remaining ownership maps, shader-generation containers, vertex uploads or driver allocations.

Scaled-resolve downscaling writes straight into the final shared-memory GPU buffer in both Vulkan and D3D12. Allocation-free C89 view planning handles descriptor alignment, source offsets above 4 GiB and bounded dispatches. This removes the scratch GPU allocation, growth wait and subsequent buffer copy; the necessary format transform and normal CPU read-fault synchronization remain. GPU differential fixtures are documented in [`tools/tests/data/README.md`](tools/tests/data/README.md). Gameplay frame-time gains have not been measured.

On Vulkan devices without sparse scaled-resolve support, scaling uses one coherent buffer reserved at initialization for the scaled 512 MiB guest address space (2 GiB at 2x2; 8 GiB at 4x4). This removes duplicate overlapping backing, unsafe range reassignment, payload copies and gameplay buffer growth, at the cost of reserving the complete address space. Initialization fails if the device cannot allocate it. Sparse-capable devices continue committing touched ranges on demand. Existing direct resolves bypass intermediate EDRAM dumps only when guest layout and ownership permit it; required format/layout transforms remain GPU operations.

The core option Graphics API picks Xenia's GPU backend: Vulkan, or on Windows Direct3D 12. The core asks RetroArch for that kind of context, and RetroArch switches its video driver to match when it is allowed to (Settings > Video > Output). Direct3D 12 runs on the system's Direct3D 12 runtime: the Agility SDK runtime that standalone Xenia uses can only be loaded by an executable that exports it, which RetroArch does not. D3D12 requires Shader Model 6.6 support from the active runtime and graphics driver for native descriptor-heap indexing.

## Content

Analog sticks preserve the frontend's raw 16-bit values; the core adds no stick dead zone before the title reads XInput state. Configure frontend dead zones as desired; games retain their own filtering. Y inversion saturates its unrepresentable positive endpoint, and analog triggers round once to the Xbox controller's 8-bit format, preserving physical 8-bit levels through frontend expansion. This follows [XInput's application-side dead-zone policy](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput).

Input state uses a C89 mailbox with 16 immutable snapshot slots per port and a bounded 64-event keystroke queue. Guest reads borrow a pinned snapshot directly for guest-format conversion. A preempted reader cannot block publication; if every alternative slot is pinned, the previous complete state remains available. Key storage and repeat tracking are fixed at creation, and 400/100 ms repeats follow frontend progress. Atomic motor pairs are applied through the rumble callback on the frontend thread; disconnect requests stop the motors. Snapshot/keystroke/rumble handoffs use libretro-common atomics without a state mutex. Frontend polling/device configuration is single-owner, and every guest reader must finish before driver destruction. Cold device enumeration still uses C++ metadata.

Startup does not display the standalone Windows disclaimer dialog or read/write its registry acknowledgement.

Guest text notices use eight fixed 1024-byte C89 slots and libretro-common atomics. Producers copy temporary strings once; `retro_run` borrows the selected slot through the message callback without a mutex or intermediate string. Notices coalesce, oversized UTF-8 text is truncated, and saturated storage may drop a notice.

Guest UI requests complete headlessly: message boxes choose an offline or confirmation answer, keyboards retain their default text, and storage selection uses the first available hard disk. UI open/close notifications use a bounded libretro-common queue drained by `retro_run`; the close delay follows frontend progress. Standalone windows, menus, native dialogs, swapchains, UI scaling/overlay pipelines, immediate-mode drawers and their shader assets are removed. The presenter only maintains guest output for frontend GPU handoff and capture. No standalone UI timing thread or sleep loop is created. There are no bundled UI fonts or fontconfig dependency. Guest profile/tile image decoding remains available independently of the UI.

Xbox 360 games as a disc image (`.iso`), an executable (`.xex`), or a `.zar` / `.xcp` package. No BIOS or firmware is needed.

Original Xbox games run through XeFu, the Xbox 360's own emulator of the original Xbox, which upstream Xenia Edge can run. It needs XeFu's files from a console (`xbox.xex` and the `xefu*.xex` builds of the system partition's `Compatibility` folder) in `<system>/Xenia-Edge/xefu/`; the game is opened as a disc image (`.iso`), a full dump or one rebuilt to the game partition alone. Only the games the console itself could run have a chance.

Everything the core keeps is in `system/Xenia-Edge/`: saves, title updates and DLC under `content/`, laid out as in standalone Xenia's content folder; the shader and other caches under `cache/`; and Xenia's log, `xenia.log`. Errors and warnings from it also appear in RetroArch's log. The core option Apply Title Updates turns updates off.

Fibers use owned libretro-common libco AMD64/ARM64 backends, including direct AMD64 switching under MSVC. A C89 lifecycle adapter reuses guarded stack allocations on restart without copying stack contents. On Windows, the fiber stack exception callback is removed when the last stack is released and explicitly detached after emulator shutdown, including when retired guest frames retain stack owners. Any retained owners are reported in `xenia.log`.

Vulkan EDRAM helpers reserve a GPU-command-thread-owned 64 KiB output buffer and 128 KiB of instruction slots at initialization, freeing both on shutdown. The instruction workspace is reused without page allocation for the tested shader matrix; larger valid modules can grow it in checked 8 KiB pages. This trades bounded startup storage for fewer runtime allocations. Guest translators also reserve 128 KiB each, retaining their own instruction pages across shader resets. Both these helpers and guest workspaces reserve 16 KiB of C89 ID-to-instruction lookup storage for 2048 IDs. Modules borrow that index from their exclusive instruction workspace instead of owning a vector. Logical reset invalidates all IDs without clearing retained storage; sparse holes are initialized only when the new module reaches them. Checked growth retains higher capacity until workspace shutdown, avoiding repeat lookup allocations and growth copies after warming. Bounds-checked reads reject IDs from the previous shader. This retains lookup memory between shaders rather than freeing it per module. Instruction ownership remains with the existing typed owners and arena cleanup. Function emission and post-processing iterate the builder's existing owning list directly; the duplicate raw-pointer function registry is removed. Entry-block and build-point pointers are published only after function ownership succeeds. Function parameter records are represented by an eight-byte C89 contiguous ID range, replacing their pointer vector. Emission and destruction borrow a checked pointer view from the module index while it remains immutable; no additional parameter array is built. Parameter cleanup remains before block cleanup, with partial constructors reclaimed by the existing arena unwind. Function block ownership is an append-only C89 chain using a plain, nonvirtual link base in each block. Typed casts recover blocks without layout arithmetic. This removes ownership-vector allocations and pointer copies, preserves creation order, and rejects duplicate or foreign adoption. The unused removal API is gone. The link cost is offset by replacing two optional debug trackers with C89 values and shared validity bits: blocks remain 136 bytes on tested x64 builds, and function owner metadata remains three words. Teardown caches the next link before typed deletion without rewriting dead ownership state; C++ callbacks enforce a nonthrowing contract. CFG membership uses the immutable owner and dense index instead of reading a duplicate pointer array. Runtime and cache-loading workers never share the workspace; larger shaders retain their checked high-water capacity until worker shutdown. The four guest argument scratch vectors are replaced by C89 owners with 64 inline words each (1 KiB of additional inline storage per translator). Checked growth retains larger high-water capacity until shutdown, without a fixed guest-shader limit. Builder arguments are borrowed synchronously, and four fetch/swizzle paths borrow their source arrays directly instead of first copying them into scratch. Entry-point interfaces likewise use retained C89 scratch (256 additional inline bytes), with one exact final-instruction reserve. Loop-continuation incoming pairs go directly into their final instruction instead of a separate vector and copy pass. Detached phi and switch owners retire before builder reset or arena shutdown. Operand buffers and other IR containers still allocate. Transfer and dump generation reuse this storage; dump output no longer passes through a second full-module vector copy. Direct-resolve packing uses eight stack words for its two 16-byte stores. Vulkan borrows its words only during synchronous shader/pipeline creation. Checked geometric growth retains larger high-water capacity without a guessed shader-size limit. Raw-SPIR-V IR instructions are concrete, non-polymorphic 64-byte records on 64-bit hosts, down from 72 bytes. Blocks, functions and modules likewise remove unused vtable pointers. Final types prevent a future derived object from being deleted through these records; deleted copy operations prevent shallow operand-owner assignment. Destruction still follows the existing ownership graph. Instruction records use 64-byte-aligned C89 arena slots. Normal owners run typed cleanup and retire slots; the module reclaims unadopted instructions during exceptional exits, after the builder's other IR owners are destroyed. A relative page offset fills former record padding, preserving the 64-byte payload. Pages retain storage until the module scope ends or a borrowed workspace closes. ASan checks retired-record access. Both EDRAM helper emitters now use bounded C89 ID scratch and fixed arrays instead of scratch vectors. Borrowed builder APIs consume these views synchronously; nested access-chain type walks use the supplied indices directly without a second mutable-state copy. The shared builder retains vector entry points for other callers, forwarding them to the same borrowed implementation. Other IR owners still allocate. Module-process metadata also owns encoded C89 words rather than a vector of borrowed string pointers, fixing a temporary-string lifetime bug. Guest shader outputs also use C89-owned word storage: a sizing walk reads instruction lengths and metadata sizes without encoding a second module, followed by one exact reserve and direct serialization into the final allocation. CFG traversal replaces visited/delayed/reachable hash sets with C89 byte flags indexed by each block. Unplanned traversals use 256 inline bytes, with one checked temporary allocation for larger functions; ordering, structural dead-block roles and callback exceptions are preserved. Planned serialization borrows larger-function flags from the retained plan allocation instead of allocating walk scratch. The index uses existing block padding on 64-bit hosts. Translation publication and depth-helper returns transfer ownership without copying; readers borrow immutable byte views. No output vector, full-module conversion copy or zero-fill is needed. Sizing records a borrowed block order in retained C89 storage; emission replays that order without a second CFG walk. Each plan has 64 inline pointers and checked geometric growth; its heap combines pointer slots with one byte of traversal flags per slot. Each function reserves its upper bound before traversal so callbacks cannot move borrowed flag storage; translation workers retain larger capacity across shaders. Logical entries clear on every successful or failed serialization, before IR destruction. Builders retain a private fallback plan for callers without worker storage. Plans are exclusive to their compiling owner; sizing and emission still visit instruction lengths and payloads separately. The typed owner is eight bytes larger than the former vector on 64-bit hosts. Other IR containers, operand buffers and driver compilation allocations remain.

The private `xenia/base/atomic.h` shim is removed. CPU reservation tables, guest lock/list primitives and executable-info flag updates use libretro-common atomics directly. Reservation counters retain the four-byte layout used by generated code. Packed SList snapshots load atomically and avoid local type-punning; ARM64 list completion keeps the required full barrier. Modern MSVC x64 spinlock release uses a plain aligned release store. XMA context/trampoline bitmaps use a C89 word owner, allocated only on cold resize; acquisition/release use atomic reads and CAS without allocation. Reset/resize/close require quiescent users. Unused vector-copy/import APIs and three unreferenced global-lock builtin registrations are removed. MSR instructions continue using per-context state; a reserved word preserves context offsets. Required guest locking behavior and remaining live host locks are not eliminated by this migration.

Content close flushes and joins the logging writer non-alertably before the core DLL can unload. Windows APC delivery cannot release its queue and sinks before the writer exits. Reset and graphics context rebuild share the existing logger; reopening content starts it again.

The kernel owns socket I/O. Closing content cancels live socket waits, joins the I/O worker and releases pending handlers before guest state is destroyed. Closed wrappers retained by retired guest frames keep no running worker or pending I/O context. No socket worker is joined from a process-static DLL destructor.

Synchronous guest I/O events are recycled only after both the waiting caller and signaling worker retire. A C89 common-atomic pair prevents a late completion signal from reaching a reused event; its state is confined to private host I/O events. Offloaded calls carry no host queue/run timestamps or I/O diagnostic counters. The existing guest I/O queues, call allocations and worker-pool growth remain to be replaced.

Each `retro_run` supplies one video callback and one run's audio duration at the declared refresh rate. Fractional sample counts carry forward; audio underruns and a missing mixer produce silence for the remainder. Muting drains audio without adding a backlog. Stereo frontends may use either the batch or per-sample callback.

Vulkan and D3D12 collect orphan host render targets above 512 MiB after new allocations, with 120 guest frames between collections. This threshold controls reclamation attempts, not a hard VRAM cap: active EDRAM owners and resources still used by GPU submissions remain alive. Ownership collection marks existing targets directly instead of allocating a temporary ownership set. Empty collections preserve warm Vulkan framebuffers and D3D12 bindings without allocating a retirement batch. Resource retirement uses a C89 queue without an extra GPU wait. Vulkan framebuffers retire before images and render passes remain cached for async pipeline jobs; D3D12 descriptors stay alive with their resources until submission completion. This addresses a source of runtime retention; the Tekken Tag Tournament 2 slowdown still needs a gameplay retest.

Paused guest CPU workers use a C89 libretro-common eventcount gate, woken directly by resume and shutdown rather than polling every 2 ms. Guest time resumes before dispatch workers wake. The existing inactivity-based pause detector and scheduler watchdog timing remain separate work.

The frontend controls presentation cadence. Guest vblank interrupts consume requests from `retro_run`; the core has no host swap limiter or Frame Wait polling option. The audio handoff has no queue-driven time-stretch or drift correction. Non-48-kHz media streams receive fixed sample-rate conversion to the 48-kHz output rate, preserving their duration.

RetroArch owns the negotiated Vulkan device. The core releases its non-owning wrappers before unload and supplies no late device-destroy callback, so threaded video can destroy a cached device after the core DLL is gone.

Vulkan context negotiation v2 routes device creation through the frontend wrapper, allowing the frontend to merge its device requirements. Older frontends and rejected v2 negotiation use v1. D3D12 requests render interface v2 and uses its fenced sync-index handoff when available, with v1 fallback. This does not implement Reflex or DLSS. D3D12 teardown waits are bounded; a stalled GPU may retain system COM resources rather than release memory still in use.

Guest audio uses libretro; miniaudio and the standalone achievement sound helper have been removed.

At content load, the core negotiates native float audio when the frontend supports it, including float-only multichannel output for 5.1. It keeps that format until unload and mixes directly into the final float buffer. Older frontends receive the classic int16 batch or per-sample output; conversion occurs only at that boundary. This removes a redundant integer round trip, not the remaining audio scheduling, resampling or queue work still being refactored.

Legacy audio conversion uses 32768 scaling, saturation and nearest-even rounding, preserving every int16 value through a normalized-float round trip. This output-boundary conversion does not change the console's guest-visible XMA integer format.

Vulkan texture and fallback scaled-resolve memory now use an owned C89 block heap; VMA and its dependency tree are removed. Cold probes warm image and linear-resource blocks, normally 64 MiB each. Metadata has 65,536 fixed span slots and at most 256 backing blocks (also bounded by the device allocation limit and cold memory-budget headroom). Optimal images and linear resources never share a block; required dedicated allocation is honored. Returning memory requires the renderer to finish GPU use first. No allocator GPU wait or release telemetry is added. Pool exhaustion, new incompatible memory types and required dedicated resources can still allocate driver backing; complete allocation-free gameplay and retail memory/performance validation remain unfinished. Other Vulkan raw-memory paths remain separate.

Vulkan driver-cache loading uses a C89 file-blob owner: a borrowed mapping on supported native files, or one exact VFS read allocation. The owner remains alive through pipeline-cache creation. Saving writes the driver output buffer through common VFS. These opaque driver blobs require a contiguous complete input; incremental/sparse description-cache loading and remaining vectors are separate work.

Shared Vulkan/D3D12 shader/pipeline persistence uses libretro-common VFS and C89 transfer/repair helpers. Microcode lengths are checked against remaining file bytes before allocation. Loading borrows mapped microcode directly or reuses one C89 range buffer for non-mappable VFS input; the shader retains its own bytecode and required endian conversion. Callback views expire before reuse and mappings close before repair. Read/allocation failures preserve the cache instead of truncating it as corrupt, including failures while reading either cache header. The writer encodes with fixed 4 KiB scratch. Persistent layouts and hashes are unchanged. Pipeline-description vectors are removed: native input uses borrowed file views, and VFS input without a mapping hook reuses a 64 KiB window. C89 code validates row hashes and field bounds; one reusable C++ descriptor is decoded for existing backend interfaces. Validation also collects translation keys, avoiding an extra file pass. Views close before repair and remain owned through replay; append-only writes preserve the snapshot prefix. Persistence runs on a common rthreads worker with a fixed 16,384-slot C89 SPSC queue. The GPU command processor is its sole producer; the producer copies the hash and existing descriptor directly into one owned slot without a temporary concatenation record, and the worker borrows that slot through each VFS write. One descriptor copy remains to retain its bytes after the caller returns. Queue storage is allocated at initialization, flush barriers are ordered, and shutdown drains and joins before closing files or releasing shader owners. A full queue applies backpressure to preserve records; this is bounded asynchronous persistence, not a guarantee that disk stalls can never reach the producer. C++ descriptor types, translation-key sets and cold path construction remain to be migrated. The captured Tekken cache-write failures still require a loaded-game retest.

Ordered async pipeline publication uses a C89 4,096-slot window reserved at initialization. Completion workers construct into reserved slots; common atomics serialize the in-order store without a publication map or mutex. Reservation backpressure occurs before the request lock so workers can continue dequeuing. Shutdown joins workers before destroying unpublished handles and retained CPU objects directly in their slots, without a temporary vector. Compilation requests also use a fixed 4,096-slot C89 single-producer/multiple-consumer ring. Requests construct directly in their owned slots. Workers claim atomically, compile directly from those slots and store unordered warm-up results without another request move. Ordered publication holds a pointer and handle while the original request slot remains borrowed; checked leases release that slot after store/discard, without moving the request into another buffer; separate producer/consumer/completion cache lines avoid cursor sharing. Common rthreads own the workers, preserve 4 MiB stacks and finish context setup before thread configuration returns. Ready work wakes one parked worker, while full queues apply bounded backpressure. Completion callbacks use an atomic arm/recheck handshake and a short common lock only for callback ownership; ordinary unregistered completions avoid that lock. The C++ request queue, thread vector, condition variable and completion event are removed. Backend deferred-destruction containers and other synchronization/storage remain to be migrated. Gameplay allocation freedom and frame-time improvements are not yet established.

`bash tools/tests/run_pipeline_handoff_bench.sh` compares metadata handoff with commit `8ce2f22a` using 8-byte and 80-byte POD requests, one/six workers, five timed batches and no driver compilation. Allocation capture is test-only and covers module-owned requested bytes, not total RSS or driver/common-runtime allocations. The 80-byte model reserves 320 KiB less storage; the 8-byte model is unchanged. Timings vary by compiler, scheduling and request size; these results do not establish a gameplay/FPS improvement.

Remaining decode-pool, PCM-copy and SIMD work is tracked in [the audio optimization plan](libretro/AUDIO_OPTIMIZATION_PLAN.md). Existing SPSC cursor isolation and native direct-span conversion are the baseline; the plan preserves the working audio schedule.

Frontend audio mixing and integer conversion use fixed reusable per-run storage, with no lazy allocation or buffer growth in retro_run. Audio queues use owned libretro-common SPSC spans with a logical 64 ms bound per driver. The producer waits for consumption notifications when full and never overwrites unread samples. Native 48 kHz production converts directly into queue spans, without a temporary converted block or its copy into the ring; volume is applied during conversion. A plain non-mirrored ring needs at most one six-channel frame of wrap scratch. Mixing reads queue spans directly, avoiding a pop-to-scratch copy. Borrowed guest buffers still require queue-owned storage before guest reuse; this is not zero-copy across the entire audio path. Teardown cancels pending writes before existing worker joins, including subsystem rebuilds. Driver controls and the bounded C89 registry use native common atomics. Per-stream read leases protect retirement without a shared mixer lock; retired buffers are collected during cold creation or teardown, and readiness notifications are batched once per mix. The frontend owns the registry for the loaded content; independently cancellable subsystem groups let rebuilds replace the C++ audio system without invalidating the frontend mixer or replaying stopped audio tails. Native client pumping temporarily uses the pre-446c545f deadline schedule as a compatibility baseline while guest time remains host-driven. Frontend consumption no longer interrupts those deadlines. Pause/resume still uses the common C89 gate. A frontend-driven replacement must pass sustained gameplay audio testing before replacing this baseline. The media worker waits on its existing work fence rather than polling every 500 ms. Non-native media resampling, upstream client synchronization and scheduler watchdog timing remain unfinished; this does not establish a retail frame-time or fast-forward audibility result.

The ZArchive reader is now C89 code using owned libretro-common streams/VFS and rthreads, with direct C calls from the core. Native immutable mappings lend metadata and compressed input without copying or endian-converting whole tables. Validated contiguous raw spans copy directly to the final destination without a decoder/cache lock. The bounded 64-block decoded cache allocates its 4 MiB payload only when needed, avoids per-block container allocations and publishes only complete, successfully validated blocks. Frontend VFS and unsupported-mapping paths retain exact reads and bounded reusable scratch. Reader operations must be quiesced before close. Mapped slices and ISO/STFS transfers use C89 overflow-safe range checks; ISO mappings clamp to the remaining logical file. Container reads propagate missing/short backing data as errors. Views still borrow their backing owner. STFS/SVOD extent vectors are replaced with owned C89 storage that coalesces adjacent same-fragment ranges at load. A fixed-stack reader preflights the requested metadata/mapped ranges and fragment presence before issuing coalesced reads directly into the caller buffer. It allocates no read-path storage; live backing-I/O failures retain the reported prefix byte count. Serial guest I/O uses a C89 intrusive libretro-common MPSC mailbox: producers publish existing request nodes, and the single worker drains FIFO batches without a queue mutex or payload copy. Mailbox storage is allocated at scheduler construction. Stop/join precedes cancellation of pending and incoming messages. Request objects/closures still allocate, workers still start lazily, and the concurrent pool/native handle migration remain unfinished.

Opening an archive validates section arithmetic, names, file extents and directory ownership/connectivity without recursive validation. Archive read failures propagate to guest file I/O. These changes do not complete the wider file-I/O/std::filesystem migration, global memory-safety work, or existing recursive device mounting.

XEX metadata uses an allocation-free C89 parser with checked optional-header ranges.
Native files use the common-backed immutable input reader; raw mapped archive
metadata is borrowed, with bounded reads for compressed or unmapped input. Mapped native fields are borrowed, and only the final module-name
string is owned by the C++ metadata interface. Declared header sizes do not trigger
whole-header allocations. This covers metadata extraction, not the separate guest
executable loader.

The full-archive footer integrity hash remains unchecked as before; decoder frame checksums are enforced when present. Component correctness tests and hardware-budget measurements are described in `tools/tests/README.md`.

Host-side XEX block/image and title-update hashes now use common SHA-1. Account
HMAC uses common streaming SHA-1/key derivation with borrowed input spans and
full digests written directly to the destination; short digests use bounded
scratch. Account RC4 runs in C89, supports exact in-place operation and checks
key/output sizes and partial overlap. Guest SHA-256 uses a C89 adapter over common
streaming state, preserving guest buffer bytes and counter wrapping. One-shot
guest MD5 uses stack state rather than a leaked heap context; guest one-shot
HMAC-SHA-1 also shares the span adapter. Streamed guest MD5/SHA-512 now use
C89 stack state and explicit byte layouts instead of allocating FFmpeg contexts.
SHA-512 finalization uses the correct 200-byte state. Guest SHA-1 and streamed
HMAC also use C89 common adapters; TinySHA1 is removed. SHA-1 length padding is
correct above 512 MiB, and streamed HMAC pads short keys over a full block while
preserving its 64-byte key limit. Other guest AES/DES/RSA state and
remaining third-party crypto callers still need their own migration and tests.

Unused RapidJSON and pugixml sources have been removed. XLast metadata uses
owned libretro-common XML parsing with C89 indexed queries for localized strings,
product information, presence, properties and matchmaking metadata.

Selected RetroArch libretro-common sources now provide XXH3/XXH64 hashing and DEFLATE decompression. Persistent hash values remain compatible. Embedded patch data decodes directly into its final allocation, using its generated raw size; XLast requires complete, checksum-valid gzip data with the declared output size. Threading and file-I/O migration to libretro-common is still in progress. Source provenance and local changes are recorded in `third_party/libretro-common/SOURCE.txt`.

The archive writer uses libretro-common streaming SHA-256 with inline state and
unchanged digest bytes; complete SHA input blocks bypass the partial-block buffer.
Runtime SHA-NI dispatch is supported by modern MSVC as well as GCC/Clang.

ZArchive uses libretro-common rzstd for its complete 64 KiB blocks. Existing standard frames remain readable; newly compressed output may have a different size. Each reader allocates decoder scratch on its first compressed block and reuses it, writing output into the archive cache. Raw blocks need no decoder scratch. The old Zstandard dependency is removed.

Games that need a signed-in profile get one: the core signs in the first profile under `content/`, or creates one when there is none, named after the first line of `system/Xenia-Edge/xbl_profilename.txt` ("Player" if the file is missing, which the core then writes there).

## Building

The checked-in `Makefile` and `Makefile.common` compile the core and its
owned dependency sources directly, without CMake. Use GNU Make, a C++20 target compiler and a host C compiler.
Patch data and raw SPIR-V/DXIL built-in shaders
are included in the repository; a native C89 tool embeds them during the build.
Linux, Android and macOS builds do not require Python or a shader compiler.
Linux builds enable POSIX/GNU declarations for the selected libretro-common C sources.
Version/commit definitions are tracked separately for the frontend adapter, so metadata changes do not invalidate unrelated emulator or dependency objects.
D3D12 primitive-expansion geometry shaders are generated directly as DXIL.
Render-target dumps, direct resolves and EDRAM ownership transfers also emit
DXIL directly. Host-depth pixel shaders, including float24 conversion and
occlusion counting, use native DXIL containers without a temporary bytecode
copy. Depth-only ROV shaders also use native DXIL, with ordered EDRAM access
and native depth/stencil tests. Guest vertex shaders without ALU kill instructions can generate native
DXIL, including position conversion and clip/cull outputs. Host render-target
guest pixel shaders can also compile
directly, including pixel parameters, barycentric interpolation, alpha tests,
coverage, gamma/exponent adjustment, blend premultiplication and depth modes.
Guest ROV color shaders also compile directly, including tiled/scaled EDRAM
addressing, per-sample depth/stencil, conditional outputs, all blend factors
and equations, color formats and write masks. Full overwrites avoid EDRAM
reads; merged/blended outputs skip unchanged stores. Native format conversion
retains signed fixed-point [-32,32] and finite extended-half values through
131008. Failure counters retain early-test semantics across later pixel kills.
Guest filtering-weight queries also compile natively in vertex and pixel
shaders, and pixel gradient queries use native derivatives. Weight/gradient-only shaders
need no texture descriptors. Empty descriptor-index tables reuse the existing
zero buffer instead of allocating and clearing an upload for every draw.
Native shader-model 6.6 heap sampling now handles guest 1D, 2D, 3D/stacked and cube texture fetches
in vertex and pixel stages at native or scaled draw resolution. Raw fetch constants drive
coordinate normalization, offsets, explicit/gradient LOD and bias, point
snapping, signed/unsigned view selection, integer conversion, gamma and exponent
adjustment. Wide 1D textures map directly to the cache's bounded row layout;
cube coordinates convert directly to native directions. Resource layouts are published once and retained across native variants. Only required sign views are sampled, and fetch
metadata is loaded once per used constant. Volume filtering selects runtime 3D or
array views, handles layer edges and skips zero-weight second-layer fetches.
Computed-LOD queries use native unclamped DXIL operations and select the signed
view only when all swizzled components are signed. Query-only shaders also support
scaled draws. Border-fraction queries retain
the core's existing unimplemented zero result. Scaled sampling uses resolved-resource
sizes and tracks untouched point-fetch coordinates through conditional register
writes to retain the guest texel footprint. Tracking uses two 32-bit validity
masks and immutable derivative values, avoiding duplicate register buffers.
Memory exports also generate DXIL directly, with guest format packing, endian
swizzles, stream bounds, atomic subword writes and dirty-element tracking.
ALLOC and killed invocations commit outstanding exports in guest order, and
scaled pixel exports run only at the permitted host pixel. Shared-memory reads
during export draws use the UAV without an intermediate copy. Unused export
elements skip initialization and loads; format aliases share conversion/store
bodies.
Triangle and quad tessellation now generate guest domain DXIL directly. The
checked-in raw vertex/hull helpers share matching patch/control-point signatures
and borrow the existing system constants through a root alias. Discrete,
continuous and adaptive modes need no linked conversion or temporary shader copy.
D3D12 uses owned DXIL generation exclusively; Vulkan uses raw SPIR-V.
SPIR-V operand words and ID flags use C89 storage with four inline operands and one combined allocation with packed flags for larger instructions. This removes the two per-instruction vectors; instruction records now use the reusable arenas described above. Larger operand growth, other IR containers and driver allocations remain further allocation work. Reproducible construction measurements are in `tools/tests/spv_operands_bench_results.txt`.
The build and runtime need no Mesa, Meson, Python, GLSL frontend, SPIRV-Cross,
glslang, shader compiler executable or separate DXIL validator DLL.
The native DXIL ALU, control-flow, vertex-fetch, buffer-resource and texture-sampling checks are documented in
[`tools/tests/README.md`](tools/tests/README.md). The native program layer joins
raw control flow, ALU and
fetch emitters with bounded register/constant storage. The vertex and host-target
pixel adapters connect to live draws and shader-cache warm-up, retaining generated
containers directly without a temporary bytecode-vector copy.

```sh
git clone https://github.com/libretro-fork/parallel-xbox360.git
cd parallel-xbox360
make -j8 platform=linux-x86_64
```

Every platform in `.gitlab-ci.yml` has an explicit selection:

| `platform` | Output | Requirements |
| --- | --- | --- |
| `linux-x86_64` | `parallel_xbox360_libretro.so` | Native x86_64 Linux toolchain |
| `linux-aarch64` | `parallel_xbox360_libretro.so` | Native aarch64 Linux toolchain, or `CC`/`CXX` cross compilers with a target sysroot |
| `windows-x86_64` | `parallel_xbox360_libretro.dll` | MinGW-w64 GCC/binutils and GNU Make |
| `android-arm64-v8a` | `parallel_xbox360_libretro_android.so` | `NDK_ROOT=/path/to/ndk` |
| `android-x86_64` | `parallel_xbox360_libretro_android.so` | `NDK_ROOT=/path/to/ndk` |
| `macos-arm64` | `parallel_xbox360_libretro.dylib` | Apple Silicon, Xcode command line tools, GNU Make and an external Vulkan driver |

`platform=unix` (the default) selects Windows x86_64 in MSYS2/MinGW/Cygwin shells,
the native Linux architecture on Linux, or Apple Silicon on macOS. `unix-arm64` and
`windows` are aliases. Info-cache updates do not require libc++ `atomic_ref` support.
Android defaults to API 28 and links the static C++
runtime with 16 KB page alignment. `CC` and `CXX` override the target compilers;
`HOST_CC` compiles the native C89 build tool for the build host. `CC`, `CXX` and
`AR` default to GCC/G++ and GNU ar; their environment or command-line settings
are respected. `CROSS_COMPILE` supplies a tool prefix. In a MinGW64 shell,
`make -j32` selects the native Windows target and GNU tools.
`.ci/build-windows-cross.sh` builds the same target with MinGW-w64 GCC on Linux.
macOS uses Apple's `libtool` to
create its static archive and loads `libvulkan.1.dylib` from an external driver;
the repository does not bundle MoltenVK or SPIRV-Cross.

Build products are isolated under `build/make/<platform>/`; the final core is
written to the repository root. `BUILD_DIR` and `HOST_DIR` override the build
directories. `DEBUG=1` enables a debug build; `LTO=1` enables compiler-appropriate LTO (GCC LTO or Clang ThinLTO).
`make clean platform=...` removes that target's build files and root artifact;
shared host tools remain for other targets. `make help` lists the selections.
The fixed zlib-ng configuration uses its portable C implementation.

The CMake build remains available and produces the same renamed core. Existing
`system/Xenia-Edge/` content and `xenia_*` option keys are retained.

The CI workflow `.github/workflows/libretro.yml` builds every platform, including the Android cross build.

STFS directory and hash-table payloads borrow the read-only container mapping for the device lifetime. Directory/hash bounds and parent indices are checked before access; hash cache metadata still uses an existing C++ container during loading.

Kernel completion-event reuse uses 64 fixed atomic ownership slots. A completed event enters the cache only after caller and worker retire; saturation releases its idle handle instead of growing storage. Cache misses still create a kernel event. The per-wake diagnostic signal-history ring and unused standalone lifecycle delegates are removed. Vulkan shader-module creation finishes before translation publication; readers borrow the immutable module. External JIT target interning uses append-only atomic eight-byte slots. Frontend/APU state, shared shader publication and kernel reference/event metadata use libretro-common atomic primitives. Media volume uses a C89 bit-preserving float helper; shader owners remain noncopyable. Shader publication uses a single atomic word to preserve translation-object size. Modern MSVC x86/x64 acquire/release operations use native loads/stores with compiler barriers. The wider atomic and ownership migration continues; mutexes remain in frontend/APU paths and the kernel, GPU, base and CPU.

## License

BSD, as Xenia. See [LICENSE](LICENSE).

XMA compressed-frame assembly writes directly into one reusable FFmpeg-owned packet per context. The 4098-byte capacity includes maximum frame bit alignment; FFmpeg padding follows the actual payload. All three modes share this storage policy, and retained packet references cover the actual data. This removes redundant packet backing and inline frame arrays; decoded PCM conversion and guest output-ring copies remain.

Offline shader regression: after building the core, run `sh tools/tests/run_spv_guest_workspace_tests.sh`. Set `SPV_GUEST_CACHE` to an existing read-only `.xsh` cache to exercise its guest shaders through host-render-target and interlock profiles; `SPV_GUEST_REFERENCE` compares generated bytes against a saved reference. No GPU or game process is started, and no cache data is shipped with the tests.

`sh tools/tests/run_spv_binary_bench.sh` compares the previous word-vector/copy serialization with direct C89 ownership over warmed builder modules. It reports allocation requests, output-buffer payload and elapsed batch time; these are serializer measurements, not gameplay FPS or process RSS. Set `BASELINE_ROOT` to a complete source archive from a compatible prior revision to compile and compare both revisions without mixing their IR layouts or traversal interfaces.

`sh tools/tests/run_spv_index_bench.sh` measures production module ID mapping, lookup and cleanup across warmed workspaces, including retained heap capacity. Set `BASELINE_ROOT` to a complete prior source archive for comparison. It does not measure shader generation as a whole or gameplay performance.

To measure function construction with the same revision-comparison harness, set `TEST_SOURCE` to the absolute path of `tools/tests/core_spv_function_bench.cc`. This fixture checks owning-list construction and cleanup, compares golden SPIR-V output, and reports module metadata and allocation requests separately from shader serialization.

Use `tools/tests/core_spv_parameter_bench.cc` as `TEST_SOURCE` for the same harness to exercise mixed parameter signatures and report tracked peak memory alongside construction allocations.

Use `core_spv_block_chain_bench.cc` with the revision-comparison harness to measure block-owner construction separately from serialization. The Windows-only `tools/tests/run_spv_guest_cache_bench.ps1` accepts already built parent/current cache fixtures and a read-only cache path; it compares process CPU and wall times with null-device output. Validate shader bytes separately before using that timing check. Results and the small serializer tradeoff are recorded in `tools/tests/spv_block_chain_bench_results.txt`.

Block instruction bodies and local-variable owners now use C89 intrusive lists instead of two owning vectors. Each non-label instruction reuses its former block-pointer slot as a link; labels retain block ownership separately. There are no pointer-array allocations or growth copies for these lists, and tested x64 sizes remain 64 bytes per instruction and 136 per block. The body retains its second-to-last node for merge queries; sampler rewrites splice before borrowed nodes without shifting arrays. The sampler pass keeps explicit borrowed block owners, and exception cleanup still destroys typed operand payloads before releasing arena storage. Other IR containers and operand/driver allocations remain. Construction, serialization and an offline guest-cache comparison are recorded in `tools/tests/spv_instruction_chain_bench_results.txt`; these do not establish gameplay frame-time gains. Audio scheduling is unchanged.

Module-level strings, imports, entry points, execution modes, names and constants/types/globals use plain C89 linked views over arena-owned records. The unused external-instruction registry is removed. These six lists allocate no pointer arrays and copy no ownership metadata during growth; tested x64 builder metadata shrinks by 72 bytes. After explicit owners and lookup containers shut down, a terminal bitmap sweep destroys each remaining payload and retires the records in groups. Global cleanup follows page/bitmap order; payload destructors only free independent operand storage. Sorted decorations retain their existing order and ownership. Other canonical lookup, function and CFG containers remain. `tools/tests/spv_global_list_bench_results.txt` records allocation reductions, comparable offline cached-translation timing, and the roughly 7% construction / 2% serialization regressions in the large synthetic cases. No universal speedup or gameplay frame-time gain is established. Audio scheduling is unchanged.

Function and block records now use C89 pools inside the exclusive compiler workspace. Growth adds pages without relocating objects, so borrowed IR and ID-index pointers remain stable during a module lease. Warm reuse resets metadata without clearing or copying payloads. Guest workers and EDRAM initialization prefault 64 block slots and 32 function slots, requesting about 17.6 KiB of additional storage per worker on tested x64 builds; larger inputs can grow the retained high-water capacity. Explicit owners release first, then orphan functions, orphan blocks and instruction payloads. Local fallback builders still allocate their own pages. Function input names are borrowed instead of copied into temporary strings; exported/imported names retain the necessary owned spelling. Failed and retired inline function IDs are cleared without reallocating the index. `tools/tests/spv_object_pool_bench_results.txt` records the allocation/throughput gains, higher retained-memory cost, serializer results and a byte-identical offline guest-cache comparison. Other container, operand and driver allocations remain, and these measurements do not establish gameplay frame-time gains. Audio scheduling is unchanged.

Vulkan primitive-expansion geometry helpers write directly into cache-owned C89 words, borrowed during synchronous shader-module creation and optional VFS dumps. Fixed ID scratch and directly assembled entry-point interfaces remove intermediary vectors and copies. Cache initialization reserves 4096 output words, 1024 instruction slots, 64 blocks and 32 functions (about 106 KiB requested on tested x64 builds); larger modules retain checked growth. The workspace is exclusive to the command thread or paused cache preload and closes after pipeline workers join. Other metadata, operand and driver allocations remain. `tools/tests/spv_geometry_bench_results.txt` records CPU measurements and retained-memory costs. `BASELINE_ROOT=<complete-0cae52f6a-source> sh tools/tests/run_spv_geometry_tests.sh` compares all 18,432 variants; `PROFILE=1` measures warmed generation. Common VFS initialization requests version 3 directory operations, falling back to version 2 file streams and native path helpers for older frontends.

Shader string literals now reserve their entire operand span once and encode directly into owned words. The C89 encoder accepts borrowed counted text, handles unaligned input without aliasing casts, and preserves ID flags and zero padding. Debug-source chunks borrow the original string rather than allocating substrings; direct C89 serialization also removes their temporary instruction buffers and extension-string buffers. Vector adapters retain their required encoding owner. Result-struct type scratch uses two fixed IDs. Other operand and driver allocations remain. Measurements and coverage are in `tools/tests/spv_string_bench_results.txt`; use `CAPTURE_OPERANDS=1` with the binary benchmark harness to include operand malloc in its allocation totals, and `EXTRA_CXXFLAGS=-DXE_SPV_LONG_NAME_BENCH` with the function fixture for long-name construction.
Texture destruction cancels its base/mip memory watches through their owner slots under one existing global critical region. CPU invalidation can no longer recycle a handle between an unprotected read and cancellation. The fix is shared by Vulkan and D3D12; `sh tools/tests/run_shared_memory_watch_tests.sh` exercises the forced race with production core objects, without a GPU or frontend. Details are in `tools/tests/shared_memory_watch_regression.txt`.

Arena-backed shader operand payloads now use reusable C89 pages. The latest allocation can grow in place while only its packed flag map moves; other growth copies initialized words into retained storage. Each compiler/geometry workspace prefaults 16 KiB and keeps larger high-water capacity until shutdown. Inline flag space records heap ownership once external words exist, preserving instruction size; stack instructions retain normal heap allocation. ASan checks retired payloads and completed leases. This reduces allocation traffic at the cost of retained memory. Details and measurements are in `tools/tests/spv_payload_bench_results.txt`.

Shader functions now use C89 head/tail ownership links in their inline function header, eliminating the function-owner vector and its growth copies. Post-processing and output borrow those links in creation order; output uses the terminal function pointer to avoid reloading its null link after the block graph. The unread per-function precision set is removed while global parameter decorations remain. On measured GCC x64 builds, function records shrink from 208 to 152 bytes and builder metadata by eight bytes. Remaining containers still allocate. Measurements and validation limits are in `tools/tests/spv_function_owner_bench_results.txt`.

Shader linkage names borrow counted prefixes of their immutable module-owned OpName payloads. Import/export decorations encode that view directly, without a second std::string owner or prefix buffer. Caller text may be overwritten once function creation returns; the owning name instruction remains alive until module cleanup. Measured GCC x64 function records shrink another 16 bytes (152 to 136). Other string/container and driver allocations remain. Reproduction and limits are recorded in tools/tests/spv_linkage_view_bench_results.txt.

Scalar, composite, struct and null shader constants use one exclusive C89 exact-key cache instead of two maps of vectors and a null vector. Slots borrow immutable instruction records and operand words; hash collisions still compare full opcode/type/count/payload keys. Specialization constants keep distinct IDs. Compiler workspaces prefault 4 KiB of slots and retain checked high-water growth across module leases; epoch reset invalidates views without reading retired records. This trades retained metadata for fewer repeated allocations and linear scans. Only slot metadata moves during growth. Measurements, including that memory tradeoff, are in tools/tests/spv_constant_cache_bench_results.txt.

Shader type and debug-type groups use a retained C89 registry in the exclusive compiler arena instead of two maps of vectors. Existing identity, layout and forward-reference rules still determine reuse; the registry only borrows records. Lookup scans borrow contiguous pointer views once, including the post-processing pointer-type pass. A view expires on mutation of its own group or module end; growth of another group leaves its record array in place. Compiler startup reserves 2,496 bytes of group descriptors/common-type pointer capacity on measured x64 builds, plus 32 bytes of owner state. Larger groups retain checked high-water capacity until shutdown. Growth can still copy pointer metadata; no instruction payload is copied for indexing. Measurements and limitations are in tools/tests/spv_type_groups_bench_results.txt.

Shader loop, switch and debug-scope stacks use retained C89 frame pages instead of three default-allocated C++ deques. Each page has 64 stable slots in a 64-byte-aligned frame area; growth never relocates live frames. Loop frames are constructed directly in their final slots, preserving references returned across nested loops without a temporary frame copy. Stack views follow their module owner in declaration order; builder/module cleanup retires trivial frames before IR cleanup. Compiler startup reserves one page per stack, and deeper stacks retain checked growth until shutdown. This reserves capacity; it does not guarantee physical prefaulting of every payload page. ASan checks popped/reset frames as well as instruction lifetime. Measurements and limits are in tools/tests/spv_stack_bench_results.txt.
