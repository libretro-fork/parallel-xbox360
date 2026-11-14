# parallel_xbox360 libretro core

Draw-side Vulkan and D3D12 pipeline waits now observe the requested pipeline's
publication instead of draining the whole worker queue. This reuses existing
notifications and borrows stable publication state without adding allocation,
locks or copies. Publication ordering and GPU-completion retirement are retained;
startup and strict submission-wide waits still drain when requested. The required
pipeline can still block its draw, and placeholder/fallback correctness and
large-shader compile stalls remain unfinished cold-play work.

D3D12 integer ownership transfers prepare their final native-DXIL PSOs during initialization and use a fixed lookup during supported color transfers. Parameters are constructed directly in retained deferred-command storage. The bank preserves unsigned storage views, native/emulated 2x MSAA and scaled/native crossings; cross-format normalized/gamma conversions, depth and other legacy transfers remain draw-compilation debt. Procedural RTX 5090 comparisons against the existing native EDRAM emitter pass the declared warmed GPU threshold in both orders, with exact image and work-count checks. An optional dedicated Windows CI runner compares each parent producer through a checked C89 DLL boundary, including its packet layout and resource lifetime; without that runner, hosted hardware coverage remains unverified. These checks do not establish smooth cold gameplay or remove remaining guest-shader compilation.
The default core build uses native libretro-common XMA, WMA and MP3 audio decoders and needs neither FFmpeg nor libiconv. FFmpeg sources are optional offline comparison dependencies, enabled only with `XENIA_BUILD_FFMPEG_REFERENCE=ON` in CMake or `FFMPEG_REFERENCE=1` in Make. Guest software decodes movie video; removing the host audio dependency does not remove FMV video support.

Default Vulkan builds omit host profiler timestamp pools, readbacks and per-submission timestamp commands when profiling is disabled. Guest occlusion queries remain active. This removes discarded diagnostic work, without a measured Dark Souls II frame-time improvement; see [profiling gate checks](tools/tests/gpu_profile_gate_results.txt).

Shader control-flow lowering uses direct forward edges in native DXIL and omits unused loop/call/dispatcher state. Vulkan discards redundant jump-to-next labels and conditions while retaining its real-label NVIDIA switch workaround. SPIR-V serialization filters phi inputs, names and decorations using the actual emitted block plan, borrowing original operand spans without rebuilding payloads. See [control-flow coverage and limits](tools/tests/forward_flow_results.txt); Vulkan call/return lowering now uses a guarded four-entry return stack only in shaders with calls; gameplay speedups are unmeasured. See also [subroutine checks](tools/tests/shader_call_results.txt). Actual translated vertex subroutines also pass exact-output Vulkan transform-feedback tests on an RTX 5090, with a failing pre-fix control; see [GPU coverage](tools/tests/shader_call_gpu_results.txt).

Ninja Gaiden II (`544307D5`) exposes **Hi-res Shadows** in Patch Manager for the World disc (`AFF931BFF00F8A0A`) and TU3 (`4CC4201C0C4ED58B`) executable hashes. It defaults on and keeps the game's existing 1024x1024 shadow surfaces during combat. Enable **Apply Game Patches** and restart content after changing the option. Higher shadow resolution costs GPU time. TU2 and unknown revisions remain unchanged. No Master Edition plugin or extra host shadow allocation is needed. Frontends without option categories show the patch among ordinary core options. Offline registry tests pass; gameplay shadow captures remain untested.

Dirty texture-upload batches compact their borrowed pointer list in place before dirty-state snapshots, preventing repeated conversions of the same owner. Distinct signed/unsigned owners remain separate. A typed C89 filter uses linear scans for tiny or heavily aliased batches and fixed 128-byte scratch for larger distinct sets, with no allocation or persistent tags. Clean batches bypass it. See [texture batch checks and measurements](tools/tests/texture_unique_results.txt).

Rejected pixel formats and hardware-render requests fail the content load with pending audio, disc and patch-option ownership cleared. Vulkan and D3D12 provider initialization failures return errors rather than terminating the frontend. Vulkan v2 negotiation retains its v1 fallback. Offline tests exercise real load APIs and forced provider failures; see [load-failure checks](tools/tests/load_failure_results.txt).

The CPU shader interpreter uses bounded C89 loop-stack entry and innermost aL addressing, correcting double depth advancement and out-of-range nested-loop access. Repeat loops inherit the enclosing current address and restart their own count instead of reusing stale slot progress. It retains fixed storage and adds no allocations or synchronization. Actual packed-microcode regressions cover nested loops, skip/break paths and signed address clamping. Literal repeat-loop goldens also execute through the real Vulkan vertex translator, separately from timed shader comparisons. See [shader loop checks](tools/tests/shader_loop_results.txt) and [repeat-loop checks](tools/tests/shader_repeat_loop_results.txt).

XContent container selection checks the VFS seek status (zero on success) and probes eight bytes through libretro-common VFS, then initialization reads directly into its owned header and closes the handle before payload mounting. Header operations no longer use stdio or filesystem stat calls. The tested factory/initialize sequence uses one header allocation instead of three. Legacy path-typed APIs and STFS fragment/payload I/O remain migration work. See [package header ownership checks](tools/tests/package_header_results.txt).

SVOD fragments now use an immutable C89 libretro-common VFS table. Native mappings lend directory sectors; unmappable streams use four reusable sector slots only during mount. ASCII names borrow sector bytes until final entry construction; CP1252 conversion uses mount-scoped fixed scratch. Payload reads target the caller's destination directly and retain partial-transfer counts. Fragment opens remain cold at mount; ordinary reads create no helper buffers or handles. Mapped reads avoid shared cursor synchronization; unmapped VFS reads require a per-fragment cursor gate. Native OS mappings are supported, but frontend VFS has no standard mapped-view callback. The existing guest read ABI still requires a final payload copy, and directory/tree/path ownership still allocates at mount. See [SVOD VFS and borrowed-view checks](tools/tests/svod_results.txt).


Shader texture/sampler bindings use a single exact-size C89 allocation and borrowed views in both renderers. Native DXIL publication fills final storage directly and compares later layouts without temporary vectors. MSVC x64 shader metadata shrinks from 600 to 576 bytes. First-publication allocation and the shader-generation mutex remain. See [binding ownership and shader-output checks](tools/tests/binding_storage_results.txt).

Native D3D12 vertex/domain and pixel bytecode now belongs to each exact shader translation. A C89 libretro-common atomic slot publishes an immutable binary or cached failure; callers borrow the original bytes until creators/borrowers finish. This removes two nested DXIL maps and five cache-lock sites without adding bytecode copies or per-publication storage. The slot adds eight bytes per translation on tested x64 builds; initial translation/emitter allocation, the shared binding-generation mutex and geometry cache remain. See [binary ownership and metadata checks](tools/tests/binary_slot_results.txt).


Pipeline worker callbacks use function pointers and a borrowed backend owner, removing three std::function wrappers from the C++ lifetime adapter. Request payloads still use their existing reusable C89 slots through publication. Worker contexts are destroyed after worker joins. GNU x64 adapter metadata shrinks from 144 to 80 bytes; measured synthetic throughput is mixed. See [callback ownership checks](tools/tests/pipeline_callbacks_results.txt).

Vulkan and D3D12 retire replaced placeholder pipelines through reserved C89 records and a libretro-common MPSC handoff, removing retirement mutexes and growing/temporary vectors. GPU completion still gates destruction. The cold pool costs 512 KiB per active backend; exhausted reservations select synchronous creation before shader preparation. Native entry metadata grows by one pointer. See [retirement checks and limits](tools/tests/pipeline_retire_results.txt).

Vulkan and D3D12 pipeline entries now use a C89 libretro-common publication record with full-width handles and packed control flags. Draws retain the concrete handle/placeholder comparison, and native retirement still follows submission completion. D3D12 warm-up failure grants a retry only after atomically clearing pending, preventing the old creator from clearing a new retry's bit; synchronous warm-up failures use the same path. See [pipeline publication checks](tools/tests/pipeline_publication_results.txt).

Vulkan shader translation no longer builds unused texture/sampler layout UIDs. Their registry had no readers, yet hashed/compared and copied binding arrays into a growing vector/map under a mutex. The registry, copies, mutex and per-shader once flag/UID fields are removed. Actual shader binding metadata and the command processor's Vulkan pipeline-layout cache remain unchanged. See [layout registry audit](tools/tests/layout_uid_removal_results.txt).

Texture invalidation hints now use one libretro-common atomic word per texture. GPU pre-checks no longer race plain callback-written booleans, and an unused cache-wide atomic exchange/store is removed. Base/mip writers still use the existing shared-memory critical region; readers fetch both flags once where possible. Watch ownership, reload rechecks, conversions and audio timing are unchanged. See [texture hint checks](tools/tests/texture_dirty_results.txt).

Two-pass Vulkan resolves now prepare only the destination backing they actually write: scaled output no longer requests/uploads the unused native aperture or dispatches against native storage after a scaled allocation failure. A shared checked C89 destination policy also validates D3D12 ranges. Vulkan removes a redundant scaled-residency preparation and uses 64-bit scaled descriptor lengths. Conversion shaders, resolve passes and synchronization remain unchanged; Dark Souls II GPU-time gains have not been measured. See [resolve destination checks](tools/tests/resolve_destination_results.txt).

APU client callback borrows and render credits now use a fixed C89 generation-tagged atomic table instead of eight callback mutexes and eight native semaphores. Retired slots cannot be reused until cleanup finishes, and late credits cannot reach a replacement client. Callback arguments are reclaimed after the borrow ends; self-unregister defers destruction to a cold collector. One shared metadata/producer mutex and the existing compatibility pump schedule remain. PCM conversion and delivery timing are unchanged. See [APU lifetime checks](tools/tests/apu_client_results.txt).

Audio queue and driver state can retain unread PCM plus pause/volume and the existing resampler's position/history under exclusive quiescence. Capture borrows one or two SPSC spans without moving a cursor or allocating. Restore copies external spans straight into the reusable final ring and publishes a complete frame interval; an exact unchanged live view restores without copying PCM. Stopped/retired streams, active frontend read leases, mismatched formats/phases and unsafe aliases reject. These component APIs do not pause producers themselves or enable full libretro savestates. Normal audio pacing, conversion and queue limits are unchanged. See [audio checkpoint checks](tools/tests/audio_checkpoint_results.txt).

The modern/raw XMA context uses the MIT libretro-common decoder. Setup reserves
stereo capacity once per owner; mono/stereo and supported sample-rate changes
reuse that storage without allocation. Complete compressed frames borrow guest
packet payloads; only frames crossing two payloads use the retained bounded
scratch buffer. Native decoded planes feed the existing guest big-endian S16
conversion and output ring. Mixing, frontend delivery and guest scheduling stay
unchanged. Legacy XMA modes use the same native decoder while retaining their packet/ring scheduling. XMP music uses native MP3/WMA streaming, borrowed mappings where available, and bounded data-transfer windows for other guest file sources. It submits actual float frame counts, including the final partial block, without an intermediate integer conversion. Mono music expands in place to stereo; guest audio cadence is unchanged.

Raw XMA capture and transactional restore cover all 320 owners with native
schema-2 metadata and borrowed overlap/partial-PCM views. Restore prepares every
candidate before committing; invalid input or late allocation failure leaves all
live contexts unchanged. State preparation may allocate; playback and profile
changes do not. This is decoder-internal state support, not complete libretro
savestates. Source history and pending guest PCM retain their actual layouts.

The native decoder uses bounded word reads, immutable Huffman prefix/gain tables
and scalar/SSE2 matrix and transform paths. Small transforms fill their FFT
permutation directly, and paired post-rotation writes final samples once. Offline
comparison beats the existing FFmpeg raw decoder on the measured stereo/mono
corpora while retaining float error below 3.6e-7 and guest S16 differences of at
most one LSB. This does not establish gameplay cadence or all-format performance.
See [native comparison](tools/tests/xma_native_benchmark.md) and [results](tools/tests/xma_native_benchmark_results.txt). Guest PCM conversion
and guest-ring copies remain; the entire audio path is not zero-copy.


Both renderers capture resolve ownership rectangles once per copy and classify native scale/layout while filling the existing fixed worklist. Eligibility checks and direct or fallback dispatch borrow that same list. This removes repeated ownership-tree walks and failed-direct fallback recopying without changing pixel shaders, ownership, source order or GPU commands. Whole-EDRAM trace dumps still collect fresh rows. Existing C89 classification and cold storage are reused; no new runtime allocation, lock or profiling counter is added. See [prepared ownership checks](tools/tests/resolve_capture_results.txt); CPU/GPU timing and gameplay gains remain unmeasured.

Vulkan scaled direct resolves can borrow the immutable scaled-buffer descriptors already used by texture loads, avoiding a transient destination descriptor and update per eligible resolve. The shader adds the selected view origin after scaled tiling; exact pixel packing, stores, residency and barriers remain. Range/unsupported-device paths keep transient descriptors. This reuses the existing cold bank rather than adding storage, but costs a constant load and integer add in scaled direct shaders. D3D12 already binds a GPU address directly. See [scaled resolve view checks](tools/tests/scaled_resolve_views_results.txt); GPU-time and FPS gains remain unmeasured.

Vulkan native direct resolves can use an aligned range of shared memory when a persistent whole-aperture descriptor is unavailable. A checked C89 plan rebases the existing destination constant, preserving native tiled byte addresses and memory-watch/residency handling. Eligible operations avoid the EDRAM dump and subsequent copy dispatch; out-of-range or oversized requests keep the legacy path. This constrained-device/pool-failure path uses a transient descriptor; devices with a valid whole-buffer descriptor retain it. D3D12 already binds a destination address directly. See [range-view checks](tools/tests/resolve_view_results.txt); no GPU-time or 5090 FPS improvement has been measured.

Vulkan and D3D12 direct resolves specialize destination tiling to the known 2D or 3D layout, removing the unused address calculation and runtime selection. Shared C89 arithmetic matches native Xenos addressing; source loads, pixel packing and destination stores retain their behavior. Fixed GPU-owner pipeline caches remove node/rehash allocations and borrow handles during teardown. Each backend reserves 16,640 CPU bytes; using both layouts can require an additional driver pipeline, and shader/driver allocations remain. See [address and cache checks](tools/tests/resolve_address_results.txt). This is part of accuracy-preserving 4x renderer optimization; GPU time and Dark Souls II FPS gains remain unmeasured.

Eligible Vulkan texture loads decode directly into an integer staging image, removing scratch write/read traffic and the second compute dispatch for those levels. Mixed compressed mip chains keep eligible large stored levels on that path and decode the remaining stored tail once into compact scratch. Partial logical mips sharing a packed tail stay together; array offsets and source descriptors are rebound across the two layouts. A checked C89 raw-SPIR-V rewrite preserves the decoder, and the final copy keeps the sampled image free of storage usage. Small-block, 3D, depth, unsupported devices and allocation failures retain the existing path. Retained staging/scratch growth, final copies, cold pipeline compilation and driver allocations remain. See [sink equivalence](tools/tests/spv_image_sink_results.txt) and [mip partition checks](tools/tests/texture_image_mips_results.txt); GPU timing and rendered output remain unmeasured.

EDRAM dump/direct-resolve shaders in both renderers use exact reciprocal-based source-pitch division. The CPU supplies one additional constant when binding the pitch; GPU address calculation uses 32-bit multiply-high and a bounded correction. This preserves the integer quotient/remainder across the full 32-bit input range, including pitch one, without shader int64 or approximate floating point. It adds one constant update per pitch bind; it does not remove pixel transfers or establish a measured 4x GPU/FPS improvement. See [division coverage and limits](tools/tests/gpu_divide_results.txt).

A [libretro](https://www.libretro.com/) core of [Xenia Edge](https://github.com/has207/xenia-edge), the Xbox 360 emulator, for RetroArch and other libretro frontends.

This repository follows xenia-edge and adds the libretro frontend in `libretro/` plus owned shader and build paths. It holds the core only; upstream's standalone UI, packaging and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is merged to (`upstream.version`). The core is currently on xenia-edge commit 8dc77b0 of 7 October 2026.

GPU control messages use owned libretro-common MPSC publication and C89 fixed storage instead of a shared C++ callable queue. Requests copy only their small control metadata; the consumer borrows it until completion. Setup reserves 24,864 bytes on x64 for 64 small and four path-sized requests, with explicit failure on exhaustion and no ordinary submission allocation. Shutdown preserves the worker join before cancellation and storage release. Other pipeline callbacks and existing idle waits remain. See [control queue checks and limits](tools/tests/gpu_tasks_results.txt); this does not establish a GPU-time or frame-pacing gain.

Shader-cache startup completion now crosses threads as an eight-byte C89 generation token using owned retro_atomic primitives. The completion callable, its mutex and unused callback forwarding through Vulkan/D3D12 storage APIs are removed. Vulkan's GPU owner retains only the ticket, so a worker no longer writes its plain startup flag. Existing pipeline creation/publication, startup nonblocking behavior and worker joins remain; other creator/store/context callbacks and worker parking still exist. See [completion ownership checks](tools/tests/pipeline_completion_results.txt).

Memory-state restore now preflights all nine heaps into fixed borrowed C89 views before changing live RAM. Replay groups adjacent committed pages into direct input-to-RAM copies and host commits, preserving per-page protection restoration and physical-alias metadata. No payload staging buffer is added. Invalid-format rejection is protected across heaps; host allocation/protection failures during replay still need recovery. Libretro serialization remains disabled until full CPU/kernel, GPU, audio and timing capture/restore is complete. See [memory restore checks and limits](tools/tests/heap_state_results.txt).

State-operation graphics pause now includes the frontend-driven vblank worker as well as the command worker. A setup-time C89 pause gate acknowledges only between interrupt callbacks and retains queued credits. Graphics and APU/XMA stages consume one bounded host-time budget, with ownership masks preserving preexisting component pauses. Stop still wakes and joins workers before storage is freed. No periodic timer or normal delivery buffer is added. Full snapshot coordination, GPU completion/readback and media-state capture remain unfinished; see [quiescence checks and limits](tools/tests/state_quiescence_results.txt).

Graphics and APU now share an allocation-free C89 pause controller instead of duplicating that transition logic in C++. Each tested x64 owner stores 40 bytes and borrows an immutable static stage table. It unwinds newly acquired stages in reverse order, including partially failed acquisitions, while preserving earlier pauses. Shutdown retains the existing worker stop/join order. CMake now selects one scalar retro_atomic backend across C and C++. Graphics destruction also frees its previously leaked register-file mapping; an OS-backed cold-object fixture checks 128 releases without creating a device or running guest code. See [controller and mapping checks](tools/tests/state_control_results.txt).

Vulkan scaled texture loads can borrow immutable whole-buffer source descriptors instead of allocating and rewriting source views per load. A fixed C89 bank is populated at context setup where device range limits permit; other devices and allocation failures retain transient range views. Checked 64-bit planning moves the source origin into the existing shader byte offset, with native/scaled residency and CPU-write invalidation preserved. Destination scratch and pixel-transfer passes remain. D3D12 already uses source addresses this way. See [source-view coverage and limits](tools/tests/source_views_results.txt); GPU/frametime gains remain unmeasured.

Vulkan and D3D12 direct resolves share packed source loads when a native-resolution fragment feeds a scaled destination. At 4x, eligible 32bpp runs emit two source fetches instead of eight; 64bpp runs emit one instead of four. The C89 planner only reuses coordinates proven identical, including the half-pixel fill behavior, with no new GPU branch, storage or precision change. Scaled-source fragments, odd scales, unscaled output and converting resolves retain their existing behavior. Actual shader matrices pass SPIR-V/Microsoft DXIL validation and native sanitizer checks; GPU timing and Dark Souls II FPS gains remain unmeasured. See [native-load equivalence and coverage](tools/tests/resolve_native_loads_results.txt).

Vulkan pending barriers also use retained C89 pages instead of three vectors and copied deferred arrays. Replay borrows the original native buffer/image records. Only the current dependency group deduplicates identical barriers, preserving repeated transitions after intervening ones and avoiding splits caused solely by historical overlaps. Groups split at native-array page boundaries; this can add conservative barrier commands. Cold setup reserves about 31 KiB beyond the existing 1 MiB command stream, with new pages still possible at higher demand. External temporary arrays keep the copy-owning API. Offline MSVC/ASan and MinGW replay checks cover ownership, ordering and failed-publication retries; no gameplay/GPU-time gain is claimed. See [Vulkan barrier checks and limits](tools/tests/vk_barriers_results.txt).

D3D12 command-processor barriers now construct directly in retained C89 row pages. Submission records borrowed pointer/count spans, removing the pending vector and its copy into deferred packets. Rows stay owned through CPU replay; reset retains warmed storage and preserves barriers queued outside a submission. Existing callers with temporary arrays retain their copy-owning API. Offline replay into a C COM mock verifies exact row addresses, values and ordering, with no warmed storage allocations in that fixture. Larger high-water submissions can still allocate pages, and page boundaries can split API batches. See [barrier row ownership and limits](tools/tests/barrier_rows_results.txt).

D3D12 scaled resolves now order overlapping UAV writes through fixed C89 write-history storage. The prior pending-write flag was never consumed and equal-state requests cleared it without a barrier. Tracking includes complete 128-byte scaled groups relative to each binding base, so disjoint byte spans that share expanded GPU storage still synchronize. Disjoint footprints avoid an extra UAV barrier; real transitions and existing alias barriers retain their ordering. The table uses 268 bytes per scaled virtual buffer and allocates nothing; capacity exhaustion conservatively starts an ordered epoch. GPU/gameplay validation remains outstanding. See [UAV epoch checks](tools/tests/gpu_uav_results.txt).

Vulkan and D3D12 direct resolves now omit complete workgroups that the unchanged shader would reject outside its destination rectangle. A shared allocation-free C89 planner preserves tile origins, live threads, partial groups and whole vector-store tails, with conservative fallback for wrapping arithmetic. Small padded resolves can launch substantially fewer groups; full aligned targets may be unchanged. Vulkan also publishes scaled direct writes to subsequent compute readers/writers across aliased sparse windows. No gameplay or GPU-time gain is established yet; see [dispatch coverage and limits](tools/tests/resolve_dispatch_results.txt).

Disc playlists stream through libretro-common VFS with a reusable 4 KiB line window. Complete lines borrow that window; split lines reuse spill storage, and each final UTF-8 disc path has one owned allocation. Disc Control uses C89 metadata and function/context callbacks; pending guest requests form a caller-owned FIFO instead of overwriting a single callback. Path leases survive reentrant edits/unload, and the common emulator stop path answers waiting requests before worker joins. A libretro-common control mutex remains for mutable metadata; playlist loading is synchronous, its pointer table can grow, and the existing emulator boundary still converts the chosen path to a native C++ path. See [disc I/O and lifetime checks](tools/tests/disc_results.txt).

Soulcalibur IV can load PE headers whose discardable relocation tail is omitted from the resident XEX image. Only explicitly discardable sections are clipped to resident bytes; ordinary section bounds and address-overflow checks remain strict. Failed launch cleanup clears borrowed input/graphics views before subsequent frontend calls. Synthetic exact-layout regressions run in Linux and Windows CI; an offline callback fixture checks continued video/audio delivery after failure. See [startup regression coverage](tools/tests/soulcalibur_startup_results.txt).

XEX headers and patch payloads now use owned C89 high-water byte storage; patch growth preserves explicit zero-tail semantics. Normal-compression loading uses one reusable private workspace through key retries, compacts decrypted chunks in place, and frees scratch when loading returns. Header/security extents, CBC block lengths, chunk/hash-chain bounds and image-page sums are checked. Plain/basic paths zero only the required holes and padding. This retains necessary header/patch ownership and does not remove all copies or allocations; PE/import parsing and heap resets remain separate work. Delta packets now borrow bounded wire spans with destination/reference capacities and complete hashed image-chain preflight before image moves or writes. Plaintext patches avoid staging, encrypted patches use checked scratch, and synchronous LZX wrapper metadata stays on the stack. Decoder state still allocates; patch headers/allocation changes are not transactional. See [delta checks and limits](tools/tests/xex_delta_results.txt). See [XEX loader checks and limits](tools/tests/xex_image_results.txt).

PE section discovery now validates the borrowed little-endian table and every resident section span before publishing reusable C89 snapshots. It removes unaligned typed header reads, an unused scan and incremental vector growth; the snapshot remains owned because guest writes can change the wire bytes. Repeated parsing reuses its allocation, and unload clears section visibility. This does not validate imports or all PE directory contents. See [PE section checks and limits](tools/tests/pe_section_results.txt).

Instruction flags retain their writable native mapping. Cache preparation uses libretro-common native VFS operations on that same backing path, checks size/version and repair errors, and rebuilds invalid zero backing before mapping. Only the fixed header is initialized in memory; no file slurp, whole-cache staging buffer or full-mapping clear is introduced. Lookups reject the exclusive code endpoint and incomplete mapped words. General frontend-VFS writable mapping remains separate work. See [instruction-cache checks and limits](tools/tests/instruction_cache_results.txt).

Mapped instruction facts now use explicit C89 flag masks and libretro-common atomic loads/OR operations on both reader and writer paths. This removes concurrent C++ bitfield reads and temporary bitfield-to-mask copies. Version-4 cache words are retained after C/C++ legacy-layout checks on the tested GCC and MSVC x86-64 hosts. These monotonic hints do not publish other objects or replace their lifetime synchronization. See [flag concurrency checks and limits](tools/tests/instruction_flags_results.txt).

Vulkan draw sampler snapshots now rotate two owned C89 buffers per stage instead of copying vectors after descriptor preparation. Current records remain stable through overflow retries and repeated binding updates; sampler handles stay owned by the texture cache. Each buffer reuses its high-water capacity; warming the other slot or growing beyond its capacity can still allocate. Non-placeholder descriptor counts must match collected samplers if async shader bindings become ready mid-draw. Two unused D3D12 sampler-vector members are removed. See [sampler scratch checks and limits](tools/tests/sampler_scratch_results.txt).

Vulkan descriptor image records now use checked C89 span planning and reusable final-record storage. All Vulkan fields retain their initialized values, with no temporary value-initialization followed by overwrites. Expired records are discarded on growth; retained capacity remains owned until command-processor destruction. Vulkan and D3D12 resolve-submission publication now uses libretro-common 64-bit release/acquire operations at the existing queue handoff points, preserving separate GPU completion waits. See [descriptor and submission checks](tools/tests/descriptor_submission_results.txt).

Vulkan single-layout and constant descriptor reuse keeps each handle in a stable 16-byte C89 record on tested 64-bit targets. Frames are stored once per batch, and a completed batch is spliced into the free list without walking its handles; original free-LIFO order is preserved. Each of three pools warms 128 handle records and eight frame records during context setup; both capacities survive pool resets and old handles are forgotten lazily. Metadata growth still allocates at new peaks, and native descriptor allocation is unchanged. Frame-completion checks and GPU pool-reset boundaries are preserved. See [descriptor reuse tests and CPU measurements](tools/tests/descriptor_reuse_results.txt).

Vulkan texture descriptor reuse also uses C89 storage: exact layout keys point to shared stable handle records, and completed key/frame batches splice directly into per-key free lists. No deque or free vectors remain; retirement neither copies handles nor scans historical keys. Pool resets invalidate handles lazily and retain metadata capacity; new key/high-water demand can still allocate. CPU fixtures improve across tested compilers, with compiler-dependent retained-memory tradeoffs. See [keyed reuse checks and measurements](tools/tests/keyed_descriptor_reuse_results.txt).

D3D12 bindless descriptor indices use a cold 1 MiB C89 link reservation for the actual native heap capacity, replacing the free vector and pending deque. Transient batches borrow the existing command allocators' completion lifetime and retire by splice; a fixed 64-index cache handles recent immediate frees. Descriptor metadata does not grow during play, while native command-allocator high-water creation remains. Transient/mixed CPU fixtures improve, but persistent-only loops remain slower and the fixed reservation uses more requested storage at low occupancy. Audio pacing and native GPU completion ordering are unchanged. See [D3D12 index checks and tradeoffs](tools/tests/descriptor_indices_results.txt).

Both renderers keep resolve owners in bounded C89 rows with a separate 16-bit hash index. Deletion moves links rather than live owner payloads, so fault processing borrows stable rows. Pruning reuses age scratch and preserves exact unsigned age/tie semantics. The fixed 209 KiB reservation removes owner-node and age-vector growth during play; fault serialization and native completion ordering remain. Other containers and GPU allocations remain. See [owner registry checks and CPU measurements](tools/tests/resolve_owners_results.txt).

Recent resolve-read history also uses fixed C89 storage, reserving 99,336 bytes per command processor. GPU-thread queries borrow key/frame metadata directly without a temporary list. Expiry remains based on the guest frame index (60 frames), with the original full-map prune/clear policy. This history is independent of owner eviction; no fault lease is moved into it. Node/rehash allocations are removed from this table, while read-key sets, rearm vectors and native resources remain. See [recent-read regression and measurement details](tools/tests/resolve_recent_results.txt).

Resolve watch rearming borrows one of two fixed C89 page bitmaps at submission while faults mark the other. The 32 KiB pair covers the full 512 MiB aperture, so range count or owner eviction cannot trigger growth. Coalesced runs call the existing page-based watch API outside the fault lock; no guest payload is copied. Sparse batches pay extra bitmap work and cold storage compared with a small retained vector. Required watch protection/fault serialization stays intact. See [coverage, threading and CPU experiment limits](tools/tests/resolve_rearm_results.txt).

Resolve fault readback captures final native Vulkan records or compact D3D12 spans directly into C89 storage reserved at setup. Each page key is observed once with libretro-common relaxed atomics; record leases survive later key changes through native recording and packed CPU delivery. This removes metadata growth and the duplicate Vulkan range array, while preserving edge clipping, GPU visibility, completion waits and existing fault serialization. Cold reservations are larger for small workloads, and driver/staging allocations remain. See [resolve readback ownership and measurements](tools/tests/resolve_readback_results.txt).

D3D12 Native system constants generate directly into an inactive typed CPU snapshot. A C89 pointer swap commits changed bytes without a vector-shadow copy or heap allocation. The dirty check reads cacheable CPU memory; required GPU upload copies, frame invalidation and failed-request retries remain. Two inline snapshots retain extra bounded storage, and CPU results vary with how often constants change. See [snapshot ownership checks and measurements](tools/tests/snapshot_pair_results.txt).

Vulkan deferred command packets also use the shared C89 page owner, replacing a growing vector. Recording avoids redundant packet zero-fill and growth relocation; replay borrows packets page by page, preserving native command order. Warmed storage survives reset, while new peaks allocate and page slack can retain extra memory. Source-array ownership copies and GPU payload transfers remain. See [Vulkan packet validation and CPU measurements](tools/tests/vk_command_storage_results.txt).

D3D12 deferred command packets now use checked C89 pages rather than an unchecked fixed 8 MiB allocation. Pages keep earlier argument views stable when larger submissions grow, and reset retains storage without clearing payloads. Replay borrows packet spans in order; native queue/fence ordering is unchanged. New high-water demand still allocates, source-array copies remain, and tight CPU recording fixtures show overhead from the added safety checks. See [command storage checks and limits](tools/tests/command_stream_results.txt).

Both renderers filter CPU-upload ranges in the producer's exclusively borrowed C89 scratch span. This removes the two backend range vectors and their high-water allocation. Lists with no GPU-refreshed ranges pass through without metadata copies; mixed lists compact only survivors after a hole. Actual guest-memory uploads, GPU memexport refresh and validity/barrier ordering remain necessary. See [upload filtering checks and measurements](tools/tests/upload_ranges_results.txt).

The shared C89 builder reserves the guest aperture's maximum range count at setup and writes final records directly; range-record storage never grows during play. It accepts the full 65,536-run case, uses defined shifts and bounds partial bitmap refreshes. Resident validity queries remain lock-free through libretro-common atomics on both readers and writers; watches and collection retain their required critical region. Refresh helpers may still allocate. This does not publish completed GPU data or remove native payload copies/fences. See [scan, ownership and concurrency checks](tools/tests/upload_scan_results.txt).

Memexport ownership tracking now updates bitmap words in C89. Both backends borrow runs without intermediate vectors: D3D12 emits copies in one pass, while Vulkan counts transitions and fills its final deferred copy records directly. Exact byte bounds and native barriers/fence waits remain unchanged. The GPU thread keeps the borrowed bitmap stable through recording; queued commands retain native records, not the plan. Other command storage and driver allocations remain. See [memexport reference checks and CPU measurements](tools/tests/memexport_plan_results.txt).

Vulkan sparse-memory submission writes binds directly into retained C89 pages. Final Vulkan buffer-bind records borrow these stable pages, removing three vectors, stack-record copies and submission-time record rebuilding. Context setup warms 128 records; new high-water demand can still allocate. GPU sparse-binding and semaphore ordering are unchanged. See [sparse bind checks and limits](tools/tests/vk_sparse_bind_results.txt).

Vulkan native descriptor pools now have an independent C89 ownership registry. It reserves CPU records before native creation and commits without allocation, so a later C++ search-map failure cannot lose the native pool. Static descriptor-size tables are borrowed directly, removing their allocation and copy. The full-pool vector is removed; reset retains CPU ownership pages while destroying native pools at the existing safe boundary. Per-pool counters borrow count-only 32-bit spans from retained C89 pages; descriptor type IDs stay in the shared immutable table. Borrowed C89 request plans handle fit/full/spending without temporary arrays. The usable search map is now a retained C89 ordered index; hot and indexed pages switch by pointer, preserving maximum-budget and equal-key order. Metadata can still allocate at new live-capacity peaks. See [pool ownership regression checks](tools/tests/vk_pool_owner_results.txt) and [counter storage checks](tools/tests/vk_pool_counter_results.txt), and [ordered-search measurements](tools/tests/vk_pool_search_results.txt), and [count-only budget policy](tools/tests/vk_pool_budget_results.txt).

The ELF loader scans borrowed big-endian PPC records in C89, validates load ranges before writes, and merges heap-page reservations with a fixed cursor instead of allocating header/range vectors. Payload copies go directly to guest memory and only BSS tails are zeroed. Failed reservations retire new pages while retaining explicitly lent initial boot backing and other owners. Initial state is defined and the unnecessary retained ELF-header copy is removed. See [ELF ownership and bounds checks](tools/tests/elf_loader_results.txt); full XEX parser hardening and explicit ELF unload support remain separate work.

Module inputs borrow mapped bytes through synchronous parsing. Raw contiguous ZArchive members now expose read-only borrowed mappings, while compressed or otherwise unmappable inputs read directly into one exact C89 buffer without vector zero-fill or a second staging buffer. Checked partial reads must complete; input/file scope guards also retire resources on exceptions. This does not eliminate compressed-module materialization or harden every XEX/ELF parser field. The [module input checks and limits](tools/tests/content_module_results.txt) document actual coverage.

Launch-image extraction uses checked libretro-common VFS writes and per-operation staging names, preserving the previous destination when a transfer or rename fails. Mapped input is borrowed through completion; unmappable entries reuse 64 KiB of per-call scratch instead of a 4 MiB heap buffer. Same-sized cached files are reused, while truncated or oversized files are retried. This path remains synchronous; stream/backend handles and unusually long path metadata may still allocate. See [the extraction checks and limits](tools/tests/content_extract_results.txt).

Core setup stores stateless subsystem function pointers instead of owning callbacks, preserving registrations through reset/relaunch. The single frontend input driver is returned directly without a temporary vector. Removed unused standalone package-install/profile-migration APIs and unregistered UI/process-launch callbacks; frontend disc requests remain active. Live emulator launch tickets and flags use C89 libretro-common atomics, with boundary/concurrency tests. Existing launch serialization and worker ownership still need further migration; this cleanup does not establish a gameplay speedup. See [the setup audit and validation](tools/tests/core_setup_cleanup_results.txt).

Guest output refresh borrows its synchronous callback and context across both Vulkan and D3D12 instead of copying owning `std::function` objects. Vulkan's shared-device target selection also borrows its frame context. A C89 three-slot mailbox uses libretro-common atomics and nonblocking exclusive consumer leases instead of a consumer mutex; a concurrent capture that cannot acquire a lease returns without waiting, allowing the frontend to repeat its last frame. Slot snapshots remain protected and GPU resource retention/completion ordering is unchanged. This removes callback ownership copies, not the required GPU blit or all presentation allocations. Offline tests cover concurrent producer/consumers, failed refresh, blanking, nested capture and lease moves. No gameplay pacing improvement is claimed.

The supplied architecture audit is reconciled with code evidence in [the architecture audit review](libretro/ARCHITECTURE_AUDIT_REVIEW.md). Fixed-budget and pool-exhaustion work remains part of the active optimization goal; the core is not yet allocation-free or free of shader stutter.

The Vulkan version probe writes its final SPIR-V words directly in C89 caller storage, bypassing the instruction graph and output vector. Capability metadata uses retained sorted C89 storage; capability and memory-model serialization avoids temporary instruction objects. Guest/helper instructions now construct C89 wire packets and output borrows their encoded spans, preserving operand fixups and ID flags. Final module assembly still copies packets into Vulkan's contiguous buffer. The guest translator still uses the legacy builder in `src/xenia/gpu/spirv`; its removal requires the backend migration described in [the emitter migration plan](libretro/SPIRV_EMITTER_MIGRATION.md).

Memory-export format translation reuses 536 bytes of C89 storage owned by each translator for its 19 fixed format branches. Packed IDs are generated directly in their final slots, width metadata is borrowed, and conversion helpers write caller-owned arrays without copying inactive register lanes. The source no longer uses `std::vector` or `std::array`. The measured MinGW fixture avoids six C++ allocations and 1,764 requested bytes per export translation, with byte-identical SPIR-V; CPU timings overlap. The workspace is initialized at translator construction and reused without per-export heap growth, as detailed in [the format-storage measurements](tools/tests/spv_export_cases_bench_results.txt). Other translator allocations and containers remain.

Shader module ID/type queries borrow the C89 packet index directly. The obsolete operand-only storage API is removed; packet growth retains its pooled storage and checked allocation rollback. Pointer-storage queries reject missing or malformed records. The legacy builder's remaining module/type/CFG duties still need migration.

Uniform shader composites borrow one scalar ID rather than staging repeated members in a vector. C89 source hashing/matching/emission preserves ordinary constant aliases and specialization identity; composite extraction borrows scalar operands/indices. Extension names now encode once into module-owned C89 payload storage. Sorted pointer metadata borrows those words for lookup and output; duplicates and disable/re-add cycles allocate nothing. The cold registry reserves 16 pointers, grows for additional unique names, and caches removed names until module reset. Final contiguous SPIR-V assembly still copies packet words. Measurements and lifetime coverage are in `tools/tests/spv_extension_bench_results.txt`.

Decoration generation now bypasses the typed instruction graph and set nodes. C89 keys borrow caller operands and text while checking identity; unique records encode once into module-owned words. Output borrows those spans. The unused standalone postprocessor and its decoration pruning/query APIs are removed. Core generators already emit their required features and block-local sampled images. Decoration records now have an eight-byte identity prefix, and output emits every stored record without an active-flag branch. The cold registry reserves 128 pointers and grows with checked high-water capacity. Final module assembly still copies words; other graph/container allocations remain. See `tools/tests/spv_decoration_bench_results.txt`.

Shader CFG output calls a function pointer with borrowed context, removing `std::function` from the SPIR-V builder/walk. The synchronous bridge still propagates callback exceptions after returning from the C89 walk. Other core subsystems retain `std::function` and are being audited for removal. See `tools/tests/spv_core_walk_bench_results.txt` for measured coverage and limits.

Memory-export shader generation now uses 34 direct loops over a C89 scalar mask iterator, removing its two nested `std::function` helpers and their callable copies. Scaled pixel memory-export shaders declare fragment coordinates even when they write no color target, fixing an invalid-input/null-record translation path. The actual microcode matrix and allocation/timing results are documented in `tools/tests/spv_memexport_bench_results.txt`; callback conversion elsewhere in the core remains unfinished.

Vulkan translation binding scratch uses eight-byte C89 key/variable records with sixteen inline entries per table and retained checked growth. Exact-key lookup preserves first-use descriptor order. Immutable published bindings still own separate packed records; temporary builder IDs stay local to the translator. Larger high-water binding sets can still allocate and copy live scratch records. See [binding scratch checks and measurements](tools/tests/spv_bindings_results.txt).

Vulkan texture, sampler, interpolator and memexport resource names construct directly in bounded C89 stack storage. The builder copies the final name into its owned SPIR-V operand; temporary formatted strings and string streams are removed. This is a translation-path allocation reduction, not a claim of copy-free shader assembly or measured gameplay gains. See [resource-name checks](tools/tests/shader_names_results.txt).

Shader compilation has a dedicated per-commit comparison workflow: procedural guest VS/PS translation, actual Vulkan graphics PSOs and D3D12 WARP numerical fixtures, raw per-case tail timings, parent comparisons and an optional pinned reference. Elapsed time remains the pass/fail metric; supplementary process-CPU samples, executable hashes and mandatory blocking/busy timer controls help investigate tails. Retry artifacts are kept separately. Hosted software-driver success does not certify cold-play smoothness. The existing NG2 compilation stalls remain a priority regression; hardware replay and a user-accepted last-known-good reference remain outstanding. See [the cold-play development and CI plan](libretro/SHADER_COMPILATION_REGRESSION_PLAN.md).

## Savestate work in progress

Audio/XMA pause requests use acknowledged common gates with bounded waits. Content teardown releases the decoder contexts and their retained packet buffers after joining the decoder worker.

Frontend savestate callbacks remain disabled. Native tests cover bounded state I/O, memory allocation metadata, six guest CPUs restored into fresh libco continuations, and acknowledged worker/timer pauses. Direct KeDelayExecutionThread checkpoints preserve the remaining delay, including a second restore before first dispatch. Other in-flight HLE calls, nested HLE callbacks and active APCs are rejected. Other kernel waits, GPU contents, the remaining XMA modes, audio media/mixing/cadence state and whole-machine validation still need implementation before states can be used for gameplay.

## Downloads

Frontend warning/error forwarding uses reusable fixed storage, including for fragmented and long log lines; it does not allocate strings per line.

The buildbot configuration covers Windows, Linux (x86_64, arm64) and Android (arm64-v8a, x86_64). The [Actions workflow](https://github.com/libretro-fork/parallel-xbox360/actions/workflows/libretro.yml) also includes macOS on Apple Silicon. Successful runs publish build artifacts when Actions is enabled.

- **Windows:** `parallel_xbox360_libretro.dll`, whose MSVC builds need the Visual C++ 2015-2022 runtime (GNU Make/MinGW builds link their compiler runtimes statically).
- **Linux:** `parallel_xbox360_libretro.so`, glibc 2.35 or newer; uses the C/C++ runtime, with no X11 or Wayland libraries (the CMake build also links lz4 where available). ARM64 uses the a64 CPU backend.
- **macOS:** `parallel_xbox360_libretro.dylib`, Apple Silicon only. The x64 CPU backend needs the address space below 4 GB, which a core loaded into RetroArch cannot free, so there is no Intel build.
- **Android:** `parallel_xbox360_libretro_android.so`.

## Video

Vertex/domain guest ALU sums and products use round-toward-zero with signed input/output denormal flushing. Vulkan selects hardware float32 RTZ independently of denormal-mode support. Devices lacking hardware flush-to-zero use signed input/output flushing around uncontracted native arithmetic; devices lacking RTZ and native DXIL share an exact C89 integer recipe; the GPU fallback uses a 32x32 extended product and requires no shader Int64 or Float64. Vulkan software-RTZ add/subtract/multiply emit the lane-wise C89 integer recipe once for the whole vector, with instruction-result product types shared by exact member types within each module. No per-lane reconstruction buffer or persistent value cache is added. See [software-vector RTZ checks](tools/tests/shader_vector_rtz_results.txt). Native-RTZ vertex dot products share one vector product and retain ordered scalar sums and lane-specific zero/NaN correction; no OpDot or reassociation is used. On native-RTZ devices needing manual denormal flushing, dot products also reuse the input-flush predicates for a masked vector zero/NaN correction. Products and accumulated sums reuse their known-flushed state instead of checking those inputs again; native-RTZ MAD also lends its flushed product to the add, while retaining input flushing for the independent third operand. Outputs and DP2ADD's raw third operand still receive required denormal handling. See [MAD checks and compile measurements](tools/tests/shader_mad_results.txt). Fractional parts explicitly subtract floor under the vertex rule, avoiding driver-dependent rounding to one for tiny negative texture coordinates. Native-RTZ fractional parts use one lane-wise vector floor/subtract and reuse the known-flushed input state; output flushing and uncontracted subtraction remain required. See [fraction lowering and compile measurements](tools/tests/shader_fraction_results.txt). Within each vector/scalar ALU operation, identical raw register or float-constant storage loads borrow the first operand ID. Source kind, index and addressing mode must match; swizzle/sign modifiers stay separate, and no value is cached across instruction boundaries. See [operand-lend checks](tools/tests/shader_operand_lend_results.txt). The CPU shader interpreter uses the same vertex arithmetic without changing the host rounding environment. Pixel shaders retain the prior nearest-rounding policy; this is not complete console floating-point equivalence or a confirmed fix for the reported NG2 textures. The fallback adds instructions; hardware execution, game accuracy and speed must be distinguished. See `tools/tests/float_rtz_results.txt` for offline/physical-GPU verification and limits. Software-RTZ fraction lowering also uses one vector Floor/subtract recipe instead of rebuilding scalar lanes; see [software fraction measurements and limits](tools/tests/shader_software_frac_results.txt). Native multiply/MAD zero correction borrows existing input-flush predicates for distinct and partial-alias lanes; [product predicate measurements](tools/tests/shader_product_predicates_results.txt) retain both the initial cache-asymmetric failures and separate warm/unseen-workload comparisons. Partial aliases use a bounded C89 lane-mask projection instead of vector extraction/reinsertion; see [partial-product measurements](tools/tests/shader_partial_product_results.txt).

The CPU shader interpreter used for draw-extent estimation borrows checked constant-bank views and loads only selected lanes. Constant absolute-value modifiers apply in both stages, including both sources of scalar constant/register instructions. MSVC C89 bit/bounds tests and actual packed-program regression tests cover the source reader; this is not a complete GPU interpreter or a solution to draw-path compilation. See `tools/tests/alu_operand_results.txt`.

Software-RTZ dot products retain their scalar product recipes and ordered sums. A smaller-IR vector-product experiment was rejected after first-use driver compilation worsened; the expanded [software-dot regression profile](tools/tests/shader_software_dot_results.txt) keeps this path visible in CI.

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

The [draw-path compilation contract](AGENTS.md#top-priority-eliminate-draw-path-compilation-and-compilation-waits) makes accurate ready pipelines and zero frame-critical compilation/waits a top priority. Both renderers still violate it; passing component compilation tests does not establish cold-play smoothness. See the [immediate plan](libretro/SHADER_COMPILATION_REGRESSION_PLAN.md#immediate-priority-enforce-the-draw-path-contract).

GPU trace capture and its Snappy compression dependency have been removed; the core does not record GPU trace files.

Everything the core keeps is in `system/Xenia-Edge/`: saves, title updates and DLC under `content/`, laid out as in standalone Xenia's content folder; the shader and other caches under `cache/`. Warnings and errors go directly to the frontend log; the core creates no separate log file, logging worker or payload queue. Routine diagnostic calls are compiled out before argument evaluation. The core option Apply Title Updates turns updates off.

Fibers use owned libretro-common libco AMD64/ARM64 backends, including direct AMD64 switching under MSVC. A C89 lifecycle adapter reuses guarded stack allocations on restart without copying stack contents. On Windows, the fiber stack exception callback is removed when the last stack is released and explicitly detached after emulator shutdown, including when retired guest frames retain stack owners. Any retained owners are reported through the frontend log.

Vulkan EDRAM helpers reserve a GPU-command-thread-owned 64 KiB output buffer and 128 KiB of instruction slots at initialization, freeing both on shutdown. The instruction workspace is reused without page allocation for the tested shader matrix; larger valid modules can grow it in checked 8 KiB pages. This trades bounded startup storage for fewer runtime allocations. Guest translators also reserve 128 KiB each, retaining their own instruction pages across shader resets. Both these helpers and guest workspaces reserve 16 KiB of C89 ID-to-instruction lookup storage for 2048 IDs. Modules borrow that index from their exclusive instruction workspace instead of owning a vector. Logical reset invalidates all IDs without clearing retained storage; sparse holes are initialized only when the new module reaches them. Checked growth retains higher capacity until workspace shutdown, avoiding repeat lookup allocations and growth copies after warming. Bounds-checked reads reject IDs from the previous shader. This retains lookup memory between shaders rather than freeing it per module. Instruction ownership remains with the existing typed owners and arena cleanup. Function emission and post-processing iterate the builder's existing owning list directly; the duplicate raw-pointer function registry is removed. Entry-block and build-point pointers are published only after function ownership succeeds. Function parameter records are represented by an eight-byte C89 contiguous ID range, replacing their pointer vector. Emission and destruction borrow a checked pointer view from the module index while it remains immutable; no additional parameter array is built. Parameter cleanup remains before block cleanup, with partial constructors reclaimed by the existing arena unwind. Function block ownership is an append-only C89 chain using a plain, nonvirtual link base in each block. Typed casts recover blocks without layout arithmetic. This removes ownership-vector allocations and pointer copies, preserves creation order, and rejects duplicate or foreign adoption. The unused removal API is gone. The link cost is offset by replacing two optional debug trackers with C89 values and shared validity bits: blocks now occupy 112 bytes on tested x64 builds, and function owner metadata remains three words. Teardown caches the next link before typed deletion without rewriting dead ownership state; C++ callbacks enforce a nonthrowing contract. CFG membership uses the immutable owner and dense index instead of reading a duplicate pointer array. Runtime and cache-loading workers never share the workspace; larger shaders retain their checked high-water capacity until worker shutdown. The four guest argument scratch vectors are replaced by C89 owners with 64 inline words each (1 KiB of additional inline storage per translator). Checked growth retains larger high-water capacity until shutdown, without a fixed guest-shader limit. Builder arguments are borrowed synchronously, and four fetch/swizzle paths borrow their source arrays directly instead of first copying them into scratch. Entry-point interfaces likewise use retained C89 scratch (256 additional inline bytes), with one exact final-instruction reserve. Loop-continuation incoming pairs go directly into their final instruction instead of a separate vector and copy pass. Detached phi and switch owners retire before builder reset or arena shutdown. Operand buffers and other IR containers still allocate. Transfer and dump generation reuse this storage; dump output no longer passes through a second full-module vector copy. Direct-resolve packing uses eight stack words for its two 16-byte stores. Vulkan borrows its words only during synchronous shader/pipeline creation. Checked geometric growth retains larger high-water capacity without a guessed shader-size limit. Raw-SPIR-V IR instructions are concrete, non-polymorphic 64-byte records on 64-bit hosts, down from 72 bytes. Blocks, functions and modules likewise remove unused vtable pointers. Final types prevent a future derived object from being deleted through these records; deleted copy operations prevent shallow operand-owner assignment. Destruction still follows the existing ownership graph. Instruction records use 64-byte-aligned C89 arena slots. Normal owners run typed cleanup and retire slots; the module reclaims unadopted instructions during exceptional exits, after the builder's other IR owners are destroyed. A relative page offset fills former record padding, preserving the 64-byte payload. Pages retain storage until the module scope ends or a borrowed workspace closes. ASan checks retired-record access. Both EDRAM helper emitters now use bounded C89 ID scratch and fixed arrays instead of scratch vectors. Borrowed builder APIs consume these views synchronously; nested access-chain type walks use the supplied indices directly without a second mutable-state copy. The shared builder retains vector entry points for other callers, forwarding them to the same borrowed implementation. Other IR owners still allocate. Module-process metadata also owns encoded C89 words rather than a vector of borrowed string pointers, fixing a temporary-string lifetime bug. Guest shader outputs also use C89-owned word storage: a sizing walk reads instruction lengths and metadata sizes without encoding a second module, followed by one exact reserve and direct serialization into the final allocation. CFG traversal replaces visited/delayed/reachable hash sets with C89 byte flags indexed by each block. Unplanned traversals use 256 inline bytes, with one checked temporary allocation for larger functions; ordering, structural dead-block roles and callback exceptions are preserved. Planned serialization borrows larger-function flags from the retained plan allocation instead of allocating walk scratch. The index uses existing block padding on 64-bit hosts. Translation publication and depth-helper returns transfer ownership without copying; readers borrow immutable byte views. No output vector, full-module conversion copy or zero-fill is needed. Sizing records a borrowed block order in retained C89 storage; emission replays that order without a second CFG walk. Each plan has 64 inline pointers and checked geometric growth; its heap combines pointer slots with one byte of traversal flags per slot. Each function reserves its upper bound before traversal so callbacks cannot move borrowed flag storage; translation workers retain larger capacity across shaders. Logical entries clear on every successful or failed serialization, before IR destruction. Builders retain a private fallback plan for callers without worker storage. Plans are exclusive to their compiling owner; sizing and emission still visit instruction lengths and payloads separately. The typed owner is eight bytes larger than the former vector on 64-bit hosts. Other IR containers, operand buffers and driver compilation allocations remain.

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

Shared Vulkan/D3D12 shader/pipeline persistence uses libretro-common VFS and C89 transfer/repair helpers. Microcode lengths are checked against remaining file bytes before allocation. Loading borrows mapped microcode directly or reuses one C89 range buffer for non-mappable VFS input; the shader retains its own bytecode and required endian conversion. Callback views expire before reuse and mappings close before repair. Read/allocation failures preserve the cache instead of truncating it as corrupt, including failures while reading either cache header. The writer encodes with fixed 4 KiB scratch. Persistent layouts and hashes are unchanged. Pipeline-description vectors are removed: native input uses borrowed file views, and VFS input without a mapping hook reuses a 64 KiB window. C89 code validates row hashes and field bounds; one reusable C++ descriptor is decoded for existing backend interfaces. Validation also collects translation keys, avoiding an extra file pass. Views close before repair and remain owned through replay; append-only writes preserve the snapshot prefix. Persistence runs on a common rthreads worker with a fixed 16,384-slot C89 SPSC queue. The GPU command processor is its sole producer; the producer copies the hash and existing descriptor directly into one owned slot without a temporary concatenation record, and the worker borrows that slot through each VFS write. One descriptor copy remains to retain its bytes after the caller returns. Queue storage is allocated at initialization, flush barriers are ordered, and shutdown drains and joins before closing files or releasing shader owners. Synchronous load/translation callbacks are borrowed directly without std::function wrappers or callable copies. The queue's sole producer tracks dirty shader/pipeline files after successful enqueue; clean submissions need no barrier or backend atomic flags. Vulkan submission completion drives ordered flush and completion-gated placeholder maintenance without draining the compilation queue. Vulkan/D3D12 placeholder retirement uses a cold 16,384-slot C89 pool and a common intrusive MPSC handoff, with no retirement mutex, growing retirement vector or temporary collection vector. Slots are reserved before placeholder creation, and transferred once on replacement; the GPU owner releases only completed handles. Pool exhaustion or cold allocation failure selects synchronous creation before shader/layout preparation, adding no capacity wait or draw-drop fallback. Creator failure retains the entry-owned placeholder and its reservation until shutdown. Reserved metadata is released only after creators join and the GPU is quiescent. Native entry metadata grows by one pointer; pool slots add 512 KiB per active backend. No live-driver timing/RSS benefit has been measured. Shader microcode remains borrowed through the write, while stack/reusable pipeline descriptions still require the queue-owned copy. A full queue applies backpressure to preserve records; this is bounded asynchronous persistence, not a guarantee that disk stalls can never reach the producer. C++ descriptor types, translation-key sets and cold path construction remain to be migrated. The captured Tekken cache-write failures still require a loaded-game retest.

Cache warmup analysis (D3D12) and translation (Vulkan) borrow exact-capacity C89 work records and use common-atomic bounded claims with libretro-common workers. No task payload is queued or copied between workers. The caller participates; zero/single-task batches create no background thread, and partial worker startup falls back to remaining workers plus the caller. Worker stacks remain 4 MiB. Per-worker translation counts publish once per batch. Native C++ exception glue joins all workers before rethrowing the first failure or releasing borrowed records. Cold task allocation and translator-owned storage remain; this is not allocation-free gameplay or a measured FPS gain.

Async Vulkan/D3D12 pipeline results publish to their own entries immediately after compilation. A completed draw no longer waits for earlier unrelated requests to publish. The ordered 4,096-result window and its serialization are removed. A fixed 4,096-slot C89 single-producer/multiple-consumer ring owns requests; workers borrow those slots through creation, entry publication and destruction without an intermediate result record or request move. Separate producer/consumer/completion cache lines avoid cursor sharing. Common rthreads own workers with 4 MiB stacks. Full request storage still applies bounded backpressure; this does not remove a draw's wait for its own unfinished compiler job. Completion tokens use common atomics, and shutdown joins creators before releasing entry-owned handles and retirement storage. Shared upcoming-submission reads use common atomics rather than racing a plain counter. Backend command submission and GPU resource dependencies retain their ordering. Other backend caches and synchronization/storage remain to be migrated. Gameplay allocation freedom and frame-time improvements are not established; see [publication ownership checks](tools/tests/pipeline_ready_results.txt).

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
Single-sample Vulkan color ownership transfers also have a fixed C89 bank for
cross-format numeric and mixed integer/numeric views. It prepares the 44 pairs
missing from the existing fast banks, with up to four native/scaled layouts
per pair (176 PSOs, or 44 at 1x). Initialization invokes the existing transfer
producer into reusable final SPIR-V storage, without inserting a second shader
map. Draws retain the original four-byte packed address and borrow a stable PSO
handle; the existing smaller integer and numeric-identity banks retain priority.
Actual typed views and gamma storage policy remain intact. The producer's
instruction arena may allocate during initialization, but it is not called from
covered transfer draws. No pixel staging buffer is introduced.
Image checks cover all 44 selected pairs, nine native/scaled layouts and signed
base offsets through both render passes and dynamic rendering. Separate C89
codec checks cover all 65,536 half encodings and both sides of every 7e3 rounding
midpoint; the alternate raw shape emitter remains a validation foundation.
MSAA/depth and value-converting transfers,
the corresponding D3D12 generic bank and complete guest
fallback rendering remain unresolved. GPU throughput qualification is separate
from image correctness; these component tests do not certify smooth cold play.

Matching numeric-view color ownership transfers now have initialization-owned
Vulkan/D3D12 pipeline banks. Their raw SPIR-V/native DXIL fetches and forwards
color without packing or conversion; typed resources, gamma storage and source
descriptors are retained. Matching formats share modules across attachment
classes, and a direct C89 writer publishes only 20 runtime bytes. Matched typed
image/work checks and paired RTX 5090 single-sample/mixed-MSAA throughput gates
pass in both orders for both backends. Complete-bank constructor observations
and pixel-checked first Vulkan/D3D12 transfers are recorded separately; driver-cache
conditions remain uncontrolled, and full startup and cold-play acceptance remain
unverified. Cross-format conversion and guest fallback
compilation remain unfinished. See
[identity-transfer coverage and pending qualification](tools/tests/transfer_identity_results.txt).
Generic transfer fallback work has a direct 24-byte C89 routing packet and
native GPU coordinate checks. It is not integrated into rendering; format
conversion, depth/stencil behavior and nonblocking specialization remain to be
implemented. See `tools/tests/transfer_address_packet_results.txt`.
Built-in primitive indices fill their mapped upload allocation directly through
a C89 writer with checked ranges. The immutable layout is borrowed only during
initialization; no callable owns it. Vulkan flushes that upload allocation and
rejects flush failure before publication. The required GPU upload copy remains.
Primitive-restart metadata uses a retained C89 array sized to the guest draw
limit, with one 262,140-byte initialization allocation. Converters borrow its
immutable rows while retaining the original index payload; clearing a draw
does not free storage or rescan potentially changed reset markers. See
[range ownership checks and comparison protocol](tools/tests/primitive_ranges_results.txt).
The CPU shader interpreter uses defined 32-bit wrapping vertex-fetch address
addition, with signed-offset full/mini-fetch regression tests and Linux UBSan
coverage. Existing buffer bounds and physical-mirror checks remain in place;
this is not complete guest-shader fallback coverage.
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

XMA compressed-frame assembly uses bounded native context storage. Modern/raw mode borrows complete frames and copies packet crossings into reusable scratch; legacy modes retain their assembly policy in the same 4098-byte scratch capacity. No FFmpeg padding or packet reference is required. Decoded PCM conversion and guest output-ring copies remain; this is not an entirely zero-copy audio path.

Offline shader regression: after building the core, run `sh tools/tests/run_spv_guest_workspace_tests.sh`. Set `SPV_GUEST_CACHE` to an existing read-only `.xsh` cache to exercise its guest shaders through host-render-target and interlock profiles; `SPV_GUEST_REFERENCE` compares generated bytes against a saved reference. No GPU or game process is started, and no cache data is shipped with the tests.

Vulkan sampling combines complementary signed/unsigned predicates into one conditional, retaining independent paths when multiple components may need both image views. No texture samples or buffering are added. After building the core, `sh tools/tests/run_texture_sampling_tests.sh` generates eight production-emitter fixtures; `GPU_TESTS=1` also checks their numeric results on Vulkan without opening a window or starting a game. The runtime and fixture driver are MSVC C89; the bridge only constructs typed SPIR-V. Coverage and offline structural measurements are in `tools/tests/texture_sampling_results.txt`; no gameplay speedup is established.

Texture format conversion uses a C89 plan to select only the live source components, preserving their original packed-field offsets. Typed SPIR-V construction uses scalar or compact vector arithmetic and updates borrowed result IDs directly, without allocating a new payload buffer. `sh tools/tests/run_texture_scale_tests.sh` checks the plan and emits thirty conversion fixtures; `GPU_TESTS=1` also compares their values and guards on Vulkan. Coverage, parent controls and static code-size results are in `tools/tests/texture_scale_results.txt`. Other shader/driver allocations remain, and no gameplay FPS gain is established.

`sh tools/tests/run_spv_binary_bench.sh` compares the previous word-vector/copy serialization with direct C89 ownership over warmed builder modules. It reports allocation requests, output-buffer payload and elapsed batch time; these are serializer measurements, not gameplay FPS or process RSS. Set `BASELINE_ROOT` to a complete source archive from a compatible prior revision to compile and compare both revisions without mixing their IR layouts or traversal interfaces.

`sh tools/tests/run_spv_index_bench.sh` measures production module ID mapping, lookup and cleanup across warmed workspaces, including retained heap capacity. Set `BASELINE_ROOT` to a complete prior source archive for comparison. It does not measure shader generation as a whole or gameplay performance.

To measure function construction with the same revision-comparison harness, set `TEST_SOURCE` to the absolute path of `tools/tests/core_spv_function_bench.cc`. This fixture checks owning-list construction and cleanup, compares golden SPIR-V output, and reports module metadata and allocation requests separately from shader serialization.

Use `tools/tests/core_spv_parameter_bench.cc` as `TEST_SOURCE` for the same harness to exercise mixed parameter signatures and report tracked peak memory alongside construction allocations.

Use `core_spv_block_chain_bench.cc` with the revision-comparison harness to measure block-owner construction separately from serialization. The Windows-only `tools/tests/run_spv_guest_cache_bench.ps1` accepts already built parent/current cache fixtures and a read-only cache path; it compares process CPU and wall times with null-device output. Validate shader bytes separately before using that timing check. Results and the small serializer tradeoff are recorded in `tools/tests/spv_block_chain_bench_results.txt`.

Block instruction bodies and local-variable owners now use C89 intrusive lists instead of two owning vectors. Each non-label instruction reuses its former block-pointer slot as a link; labels retain block ownership separately. There are no pointer-array allocations or growth copies for these lists, and tested x64 sizes are 64 bytes per instruction and 112 per block. The body retains its second-to-last node for merge queries; sampler rewrites splice before borrowed nodes without shifting arrays. The sampler pass keeps explicit borrowed block owners, and exception cleanup still destroys typed operand payloads before releasing arena storage. Other IR containers and operand/driver allocations remain. Construction, serialization and an offline guest-cache comparison are recorded in `tools/tests/spv_instruction_chain_bench_results.txt`; these do not establish gameplay frame-time gains. Audio scheduling is unchanged.

Module-level strings, imports, entry points, execution modes, names and constants/types/globals use plain C89 linked views over arena-owned records. The unused external-instruction registry is removed. These six lists allocate no pointer arrays and copy no ownership metadata during growth; tested x64 builder metadata shrinks by 72 bytes. After explicit owners and lookup containers shut down, a terminal bitmap sweep destroys each remaining payload and retires the records in groups. Global cleanup follows page/bitmap order; payload destructors only free independent operand storage. Sorted decorations retain their existing order and ownership. Other canonical lookup, function and CFG containers remain. `tools/tests/spv_global_list_bench_results.txt` records allocation reductions, comparable offline cached-translation timing, and the roughly 7% construction / 2% serialization regressions in the large synthetic cases. No universal speedup or gameplay frame-time gain is established. Audio scheduling is unchanged.

Function and block records now use C89 pools inside the exclusive compiler workspace. Growth adds pages without relocating objects, so borrowed IR and ID-index pointers remain stable during a module lease. Warm reuse resets metadata without clearing or copying payloads. Guest workers and EDRAM initialization prefault 64 block slots and 32 function slots, requesting about 17.6 KiB of additional storage per worker on tested x64 builds; larger inputs can grow the retained high-water capacity. Explicit owners release first, then orphan functions, orphan blocks and instruction payloads. Local fallback builders still allocate their own pages. Function input names are borrowed instead of copied into temporary strings; exported/imported names retain the necessary owned spelling. Failed and retired inline function IDs are cleared without reallocating the index. `tools/tests/spv_object_pool_bench_results.txt` records the allocation/throughput gains, higher retained-memory cost, serializer results and a byte-identical offline guest-cache comparison. Other container, operand and driver allocations remain, and these measurements do not establish gameplay frame-time gains. Audio scheduling is unchanged.

Vulkan primitive-expansion geometry helpers write directly into cache-owned C89 words, borrowed during synchronous shader-module creation and optional VFS dumps. Fixed ID scratch and directly assembled entry-point interfaces remove intermediary vectors and copies. Cache initialization reserves 4096 output words, 1024 instruction slots, 64 blocks and 32 functions (about 106 KiB requested on tested x64 builds); larger modules retain checked growth. The workspace is exclusive to the command thread or paused cache preload and closes after pipeline workers join. Other metadata, operand and driver allocations remain. `tools/tests/spv_geometry_bench_results.txt` records CPU measurements and retained-memory costs. `BASELINE_ROOT=<complete-0cae52f6a-source> sh tools/tests/run_spv_geometry_tests.sh` compares all 18,432 variants; `PROFILE=1` measures warmed generation. Common VFS initialization requests version 3 directory operations, falling back to version 2 file streams and native path helpers for older frontends.

Shader string literals now reserve their entire operand span once and encode directly into owned words. The C89 encoder accepts borrowed counted text, handles unaligned input without aliasing casts, and preserves ID flags and zero padding. Debug-source chunks borrow the original string rather than allocating substrings; direct C89 serialization also removes their temporary instruction buffers and extension-string buffers. Vector adapters retain their required encoding owner. Result-struct type scratch uses two fixed IDs. Other operand and driver allocations remain. Measurements and coverage are in `tools/tests/spv_string_bench_results.txt`; use `CAPTURE_OPERANDS=1` with the binary benchmark harness to include operand malloc in its allocation totals, and `EXTRA_CXXFLAGS=-DXE_SPV_LONG_NAME_BENCH` with the function fixture for long-name construction.
Texture destruction cancels its base/mip memory watches through their owner slots under one existing global critical region. CPU invalidation can no longer recycle a handle between an unprotected read and cancellation. The fix is shared by Vulkan and D3D12; `sh tools/tests/run_shared_memory_watch_tests.sh` exercises the forced race with production core objects, without a GPU or frontend. Details are in `tools/tests/shared_memory_watch_regression.txt`.

Arena-backed shader operand payloads now use reusable C89 pages. The latest allocation can grow in place while only its packed flag map moves; other growth copies initialized words into retained storage. Each compiler/geometry workspace prefaults 16 KiB and keeps larger high-water capacity until shutdown. Inline flag space records heap ownership once external words exist, preserving instruction size; stack instructions retain normal heap allocation. ASan checks retired payloads and completed leases. This reduces allocation traffic at the cost of retained memory. Details and measurements are in `tools/tests/spv_payload_bench_results.txt`.

Shader functions now use C89 head/tail ownership links in their inline function header, eliminating the function-owner vector and its growth copies. Post-processing and output borrow those links in creation order; output uses the terminal function pointer to avoid reloading its null link after the block graph. The unread per-function precision set is removed while global parameter decorations remain. On measured GCC x64 builds, function records shrink from 208 to 152 bytes and builder metadata by eight bytes. Remaining containers still allocate. Measurements and validation limits are in `tools/tests/spv_function_owner_bench_results.txt`.

Shader linkage names borrow counted prefixes of their immutable module-owned OpName payloads. Import/export decorations encode that view directly, without a second std::string owner or prefix buffer. Caller text may be overwritten once function creation returns; the owning name instruction remains alive until module cleanup. Measured GCC x64 function records shrink another 16 bytes (152 to 136). Other string/container and driver allocations remain. Reproduction and limits are recorded in tools/tests/spv_linkage_view_bench_results.txt.

Shader blocks retain only forward control-flow edges. The unused predecessor vector and its getter are removed, avoiding reverse-edge allocation/growth copies and reducing each measured x64 block from 136 to 112 bytes. Branch emission and successor ordering remain intact. Parent/current helper and guest shader binaries match; forward edges now use pooled C89 spans; other compiler allocations remain. Optional allocation profiling uses matching allocator flags for every linked inline cleanup path and chooses the same instruction-arena mode for compatible baselines. Evidence and measurement limits are in [reverse-edge removal results](tools/tests/spv_predecessor_results.txt).

Forward CFG edges use C89 contiguous arrays borrowed from the compiler's existing reusable payload arena. Appends preserve duplicates and order; traversal reads the span directly. The latest payload can grow in place, while relocation copies only initialized pointer metadata. Arrays expire before block retirement, and their pages remain until workspace shutdown. The descriptor keeps block size at 112 bytes on measured x64 builds. Warmed storage avoids per-block edge allocations, while new high-water demand can still grow retained pages. See [successor storage tests and allocation measurements](tools/tests/spv_edges_results.txt).

The shader builder no longer retains a source-expression access chain, saved l-value/r-value state, deferred swizzle flags or unused high-level scalar/matrix constructors. Raw OpAccessChain, direct swizzle views and explicit load/store memory-access operands remain. The core adapter calls the raw view API directly, removing its two state clears per access-chain build. Measured GCC x64 builder objects shrink by 88 bytes; helper allocation counts stay unchanged. An idle CPU geometry fixture measured a modest roughly 3% improvement, with shader binaries identical. Scope and reproduction are recorded in [stateless builder results](tools/tests/spv_stateless_results.txt).

Scalar, composite, struct and null shader constants use one exclusive C89 exact-key cache instead of two maps of vectors and a null vector. Slots borrow immutable instruction records and operand words; hash collisions still compare full opcode/type/count/payload keys. Specialization constants keep distinct IDs. Compiler workspaces prefault 4 KiB of slots and retain checked high-water growth across module leases; epoch reset invalidates views without reading retired records. This trades retained metadata for fewer repeated allocations and linear scans. Only slot metadata moves during growth. Measurements, including that memory tradeoff, are in tools/tests/spv_constant_cache_bench_results.txt.

Shader type and debug-type groups use a retained C89 registry in the exclusive compiler arena instead of two maps of vectors. Existing identity, layout and forward-reference rules still determine reuse; the registry only borrows records. Lookup scans borrow contiguous pointer views once, including the post-processing pointer-type pass. A view expires on mutation of its own group or module end; growth of another group leaves its record array in place. Compiler startup reserves 2,496 bytes of group descriptors/common-type pointer capacity on measured x64 builds, plus 32 bytes of owner state. Larger groups retain checked high-water capacity until shutdown. Growth can still copy pointer metadata; no instruction payload is copied for indexing. Measurements and limitations are in tools/tests/spv_type_groups_bench_results.txt.

Shader loop, switch and debug-scope stacks use retained C89 frame pages instead of three default-allocated C++ deques. Each page has 64 stable slots in a 64-byte-aligned frame area; growth never relocates live frames. Loop frames are constructed directly in their final slots, preserving references returned across nested loops without a temporary frame copy. Stack views follow their module owner in declaration order; builder/module cleanup retires trivial frames before IR cleanup. Compiler startup reserves one page per stack, and deeper stacks retain checked growth until shutdown. This reserves capacity; it does not guarantee physical prefaulting of every payload page. ASan checks popped/reset frames as well as instruction lifetime. Measurements and limits are in tools/tests/spv_stack_bench_results.txt.

Transfer geometry uses one float4 rectangle instance (16 bytes) instead of six expanded float2 vertices (48 bytes), with six procedural vertices per instance in both Vulkan and D3D12. The existing CPU coordinate calculations and each backend's triangle order are preserved. Initialization builds the raw SPIR-V/native DXIL helper; no shader-language compiler is added. Final upload storage, pool growth and utility pipeline cache misses remain. See [transfer geometry checks and limits](tools/tests/transfer_geometry_results.txt); this is not evidence of eliminated shader stutter or faster gameplay.

Generic transfer-shader work now includes a shared C89 runtime sample-address emitter with raw SPIR-V and native DXIL adapters. One test PSO handles changing scale and MSAA parameters across 147,448 records, including independent inverse inputs and bounds guards. It remains a validation kernel outside rendering; format conversion, ownership/host-depth semantics and prepared graphics-pipeline integration still need implementation. See [sample-address coverage](tools/tests/transfer_sample_results.txt).

The generic transfer foundation also has a shared integer-view color codec. Two metadata masks select 16/32-bit components and 32/64-bit samples without floating-point conversion or copied payloads. Procedural tests cover all 36 source/destination format pairs with one PSO, preserving raw NaN and signed-minimum encodings. The codec remains outside rendering and does not cover normalized/gamma/depth transfer semantics. See [integer codec coverage](tools/tests/transfer_uint_color_results.txt).

Physical EDRAM routing now uses a shared 92-byte CPU-prepared metadata packet, borrowed native IR IDs and exact reciprocal division. Tests cover tile wrapping, pitch, MSAA, scaled/native conversion, 32/64-bit word selection and color/depth column swaps across 186,607 changing records per backend. The kernel retains destination coordinates for host-depth reuse but performs no image access and is not yet connected to rendering. See [physical-address coverage](tools/tests/transfer_address_results.txt).

The integer transfer foundation now constructs complete raw SPIR-V/native DXIL fragment programs that fetch typed images, select 32-bit halves and write integer attachments. All 24 image/sample/target interfaces validate; actual single-sample raster tests cover all 36 format pairs and six scale classes on Vulkan and WARP using four pipelines prepared before drawing. A checked 112-byte packet feeds the shared recipe. These fully dynamic constructors remain validation tools; rendering uses the separately prepared shape-specific integer banks described above. Their MSAA image tests and failure/lifetime checks do not complete the remaining format/depth families or guest fallback coverage. See [integer image coverage](tools/tests/transfer_uint_image_results.txt).

### Draw-path contract instrumentation

Developer builds can enable `XENIA_ENABLE_DRAW_STATS=ON` with CMake, or
`GPU_DRAW_STATS=1` with Make in a separate build directory. The default is off.
This test profile counts translation, driver module/PSO creation, compiler waits,
blocked queue admission, caller-side jobs and existing incorrect stand-in/skip
paths by thread role and pipeline kind. It performs no event logging or timing.
The normal DLL exports no counter API; CI packages only that normal build.

The bounded `xe_draw_stats_snapshot` API is for external test harnesses. Reset
only while producers are quiescent; snapshots are individually atomic counters,
not a transactional view. Unknown roles fail the contract gate conservatively.
The queue also offers nonblocking `TryPush` with ownership retained on rejection.
The renderer still uses blocking admission: adopting the new API requires a
ready accurate fallback first. Existing inline compilation, compilation waits,
no-op pixel stand-ins and skipped draws remain defects. These tests do not
establish cold-play smoothness or correct fallback coverage. See
[component coverage](tools/tests/draw_contract_results.txt).

Normal guest-microcode analysis gathers semantic metadata without formatting
instruction disassembly or copying it into a retained string. An explicit
nullable diagnostic sink preserves disassembly when requested; the existing
shader-dump setting opts in before analysis. This avoids diagnostic text work
on the cold shader path. Analysis containers and shader/driver compilation
still allocate, and accurate nonblocking fallback coverage remains unfinished.
Straight/forward-only shaders now omit both reentry-write arrays and their
per-instruction scratch writes, using flags already collected in the first
scan. Register-use, export and binding metadata remain active. Calls and
backward branches retain their existing tracking. The real-analyzer allocation
fixture covers58 cases; [scope and limits](tools/tests/shader_forward_scratch_results.txt)
distinguish tracked C++ requests from other allocations.

Vulkan pipeline and texture-descriptor layout caches publish immutable native
owners through an intrusive C89 cache using libretro-common pointer atomics.
Ready lookups borrow those owners without the former layout mutex. GPU and
compiler callers retain separate descriptor-input scratch; larger input growth
discards expired records without copying. Publication races may create duplicate
native layouts temporarily; losers destroy their own handles immediately.
Published owners retire after compiler joins and native consumers, with pipeline
layouts destroyed before descriptor layouts. The fixed bucket array does not
limit the number of entries; layout misses still allocate native owners and
may grow exclusive scratch. This is not allocation-free gameplay or a completed
nonblocking shader fallback. See [layout checks](tools/tests/layout_ready_results.txt).


D3D12 native geometry DXIL uses the same immutable publication mechanism with a
C89 resource owner. Ready hits borrow the published binary directly, including
cached failed generation, without the former geometry mutex/map or a payload
copy. Racing cold creators may still emit duplicate binaries; losers release
theirs and all published resources survive until compiler joins and pipeline
teardown. A cold key still allocates metadata and emits DXIL. Vertex/pixel
binding locks, draw-side compilation and accurate ready fallback coverage remain
unfinished. Optional geometry dumps use VFS and preserve the exact binary length,
which need not be divisible by four. See [ownership checks](tools/tests/geometry_ready_results.txt).


SPIR-V variants no longer serialize translation behind the shader binding mutex.
Each producer fills a final packed C89 binding owner and atomically publishes it;
readers borrow that allocation until joined shader destruction. Equivalent racing
publishers free only their own unpublished owner. Publication never copies a
binding payload or waits for a producer; conflicting completed owners are rejected.
A first publication still allocates its owner, including an empty binding set.
Native vertex/pixel DXIL emission retains a separate mutex against duplicate
binary generation. Draw-side compilation, queue waits and accurate generic
fallbacks remain unfinished; this is not cold-play acceptance.

Compiler job notification uses a registered per-worker libretro-common eventcount instead of a shared parking mutex/condition. A submission selects at most one idle worker and retains the existing direct queue-slot lease; it does not copy or allocate a new payload. Worker teardown releases cached per-thread backend resources after its final wait. Required idle parking remains; this does not remove existing compilation/admission waits or establish cold-play smoothness. See [wakeup and lifetime checks](tools/tests/job_wake_results.txt).


The C89 raw SPIR-V interpreter foundation in `libretro/core_alu_spv.c` is built
only by validation tools, not linked or selected by the core. Each fixed compute
module consumes shared packed bytecode and constant views across repeated data
submissions, including GPU-side endian correction. The original entry handles
one EXEC_END clause, three vector arithmetic opcodes, three scalar forms, direct
registers/constants, masks and position exports. A separate control-flow entry
also executes multiple clauses, bool/predicate-conditional jumps/calls/execution and returns,
retaining four return addresses instead of copying register frames. Four fixed
count/address entries support nonempty counted/repeat loops and checked temporary
reads relative to the current loop address. A shared160-byte uniform view borrows
the existing bool/loop constant layout. Unsupported work fails
without committing a position. Its 4096-step work limit requires continuation
support before general rendering use. This is not a rendering fallback: empty-loop
semantics, group-wide predicate breaks, vector predicate pushes/counter operations,
relative constants/destinations, fetches, pixel RNE arithmetic,
other stages/exports and ROP are missing. Out-of-window relative temp reads fail
rather than assuming undocumented wrapping/clamping behavior.
Direct temporary reads bypass loop-address loads and adjusted-index checks;
their masked indices remain independent of high or negative loop addresses.
Scalar SETP_EQ/NE/GT/GE retain one private predicate word, independent of the
partial-export mask. Predicated ALUs test the prior predicate before validation,
operand reads and side effects. Bool clean hints preserve numerical semantics.
Private per-invocation register storage may spill; rendering
throughput and cold-play fluidity are unverified. See
`tools/tests/alu_program_results.txt`, `tools/tests/alu_control_results.txt` and
`tools/tests/alu_context_results.txt`, `tools/tests/alu_direct_results.txt` and
`tools/tests/alu_predicate_results.txt`
for coverage and initialization costs.

Shader CI also compares a companion valid-work profile. Successful primary
records retain their bytecode, input data and native-reference checks; inactive
negative-test lanes are rejected at the bounds check before VM execution.
The full mixed/cycle workload and all fault coverage remain separate gates.
This helps distinguish valid arithmetic/clause/call/jump work from deliberate
work-budget exhaustion; it is not a pure GPU timestamp or retail shader replay.
See `tools/tests/alu_valid_work_results.txt` for scope and measurements.

Shared Vulkan/D3D12 shader microcode snapshots use a C89 owner instead of a
vector. Construction fills final uninitialized storage once, preserving 32-byte
alignment for large payloads; readers borrow immutable pointer/count views.
The snapshot copy remains necessary because guest or file scratch can change
before asynchronous consumers finish. First-seen allocation remains. See
`tools/tests/shader_ucode_results.txt` for ownership checks, timing outliers and
limits; this does not eliminate draw-path compilation or certify cold play.

The raw control validation kernel also supports ordinary vector and scalar
EQ/GT/GE/NE results using binary32 integer fields. It distinguishes these 1/0
results from predicate setters, preserves P0 and scalar history, and checks
denormals, signed zeros and unordered comparisons independently of host float
modes. CI verifies execution of the additional comparison cases. This kernel
still lacks full shader/stage/ROP coverage and is not selected for rendering.
See `tools/tests/alu_compare_results.txt` for scope and measured costs.

The control kernel now reads operand slots only when its supported operations
consume them. It skips vector work without an effective result, including
literal-one export overlap; scalar history and predicate effects still execute
with an empty destination mask. This fixes erroneous rejection of unused
relative operands. See `tools/tests/alu_lazy_source_results.txt` for correctness
and cost limits; rendering integration remains unfinished.

Shader CI now measures separate Vulkan queue intervals around raw-program
dispatches. Test-only GPU dependencies order both timestamps around the work;
raw ticks, availability, width, period and a host envelope are retained.
Work-scaling and CPU-recording-delay controls reject misleading measurements.
Existing wall-time profiles stay uninstrumented. These queue intervals include
GPU synchronization overhead and do not establish isolated ALU cost, register
spills or retail cold-play behavior. See `tools/tests/gpu_timestamp_results.txt`.

The raw control interpreter now borrows immutable initial register views until a
register is written. Two private dirty words select fully defined owned values;
missing input registers read zero and partial writes preserve the other lanes.
This removes its eager register-copy/zero-fill loop, not the caller's upload or
all private storage. Inputs must remain immutable through GPU completion. The
separate raw program entry keeps its original snapshot behavior. Additional
untimed tests cover both dirty-word boundaries, zero tails, overlapping writes
and original coissue inputs. This prototype is still unlinked from rendering;
these component changes do not establish improved retail frame times or remove
draw-path compilation. See `tools/tests/register_borrow_results.txt`.

The CPU vertex interpreter used by draw-extent estimation starts dot accumulation
from its first product, matching the renderer. A synthetic leading positive zero
previously changed all-negative underflow results. The ordered RTZ/SM3 helper is
C89 and does not change host FP control modes; the pixel interpreter is unchanged.
Tests retain the original numeric corpus and add explicit signed-zero and unused
relative-source cases. This numeric correction is separate from the remaining
shader compilation and accurate-fallback work. See
`tools/tests/vertex_dot_order_results.txt` for coverage and qualification status.

The raw control prototype supports DP4, DP3 and DP2ADD using ordered RTZ/FTZ
products and scalar additions. It does not fuse or reassociate reductions;
DP2ADD alone consumes its third vector operand. Literal numeric, swizzle, mask,
coissue and temporary-write tests execute these operations as instruction data
with one prepared test PSO. This remains a compute validation prototype: no
rendering integration, complete stages/fetch/ROP or cold-play signoff is claimed.

The raw control prototype also supports vector/scalar MAX/MIN through a shared
integer-only SPIR-V helper. It preserves ordered ties, unordered selection and
selected operand bits after the existing source-denormal policy, with no host
GLSL compilation or per-record code generation. Scalar history, predicate state
and unused-source checks remain covered. Cold construction requests final bounded
module storage directly instead of growing it midway. These are prototype
coverage/ownership changes, not renderer integration or retail cold-play signoff.
See `tools/tests/minmax_results.txt` for measured costs and missing coverage.

Supported Vulkan integer ownership transfers use an initialization-owned fixed pipeline table and construct constants directly in retained deferred-command storage. Manual sample passes update only the sample index. Other transfer formats and guest shaders still have unresolved draw-path compilation; this is not a smooth cold-play guarantee. See [prepared transfer coverage and limits](tools/tests/transfer_uint_bank_results.txt).
Warmed integer ownership-transfer regression tests use one Vulkan device for
separately compiled reference and candidate banks. Image oracles and GPU work
counts precede seven alternating/reversed timing pairs. Hardware CI compares the
exact first parent and an explicitly configured pinned reference; hosted jobs
build and check the ABI/analyzer without claiming hardware timing coverage.
See [the fixture coverage and limits](tools/tests/transfer_uint_warm_results.txt).
This does not certify cold-play fluidity or resolve other draw-path compilation.
