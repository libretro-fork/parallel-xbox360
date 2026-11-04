# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream Xenia Edge are in upstream's own history; a merge is noted here with the upstream commit it brought in.

## Unreleased

- Raw SPIR-V instructions now keep four operand words/flags inline in C89 storage, replacing two growing vectors and combining larger payloads into one allocation. Common instructions need no operand allocation; byte-oracle, failure/overflow and sanitizer tests cover the change. The measured construction benchmark improves on MSVC and MinGW; instruction objects, larger operand growth and other IR containers still allocate.

- Static Make builds track frontend version/commit definitions separately, avoiding a full emulator/dependency rebuild when only build identification changes. Reported core metadata remains current.

- Removed per-call host timestamps and diagnostic atomics from offloaded guest I/O. These ran even with statistics disabled; completion, guest drive timing and worker ordering are unchanged. Scheduler statistics still report dispatch and ready-list behavior.

- Synchronous guest I/O events now return to the reuse pool only after both the waiting caller and the signaling worker retire. This prevents an early `done` observation from recycling an event before a late completion signal. The handoff uses common C89 atomics and no additional allocation.

- Shader/pipeline cache headers now distinguish failed reads from incomplete or incompatible files. A transient header read failure disables persistence without replacing the existing cache.

- Vulkan/D3D12 shader-cache loading borrows mapped microcode directly instead of filling a temporary vector. Non-mappable VFS inputs reuse one range buffer; callbacks consume views before reuse and mappings close before repair. I/O/allocation failures now disable cache loading without truncating the file as if it were corrupt. Shader-owned bytecode and required endian conversion remain.

- Ordered pipeline results now retain a borrowed request slot rather than moving the request into a second full-size buffer. Checked leases keep the original object alive until store/discard, and shutdown retires completed results before clearing unclaimed requests. Warm-up publication remains independent. Added held-slot, stale-release, object-identity and mixed warm-up/gameplay tests plus a reproducible metadata handoff benchmark.

- Vulkan/D3D12 compilation requests now use a fixed 4,096-slot C89 pool with atomic worker claims and direct in-slot consumption. Removed the shared C++ request queue, thread vector, mutex/condition variable and completion event. Common rthreads park idle workers and wake one per submission; callback locking is only armed for an actual completion callback. Worker contexts are ready before configuration returns, the 4 MiB stack budget is preserved, and shutdown still stops/joins workers before cancelling queued requests or destroying retained pipeline handles. Backend allocations and deferred-destruction containers remain further work.

- Ordered async Vulkan/D3D12 pipeline publication now uses 4,096 reusable C89 slots instead of allocating C++ map entries and taking a publication mutex. Completion workers hand off through common atomics while stores remain serialized in draw order, including failed creations. Saturated reservations wait outside the worker request lock. After workers join, each backend destroys unpublished handles and retained CPU objects directly in their slots without a shutdown vector. The request queue and deferred-destruction containers remain separate allocation/threading work.

- Vulkan and D3D12 now assemble persistent pipeline records directly in the async writer queue, removing the producer-side temporary descriptor and its extra full-descriptor copy. Compile-time layout checks retain the existing packed cache format; split-range queue tests also cover exact-byte VFS output through interrupted/short writes and shutdown draining.

- Vulkan/D3D12 shader persistence now uses a preallocated C89 libretro-common SPSC queue and file worker instead of growing C++ deques, mutexes and condition variables. Pipeline records write directly from queue slots, flush requests retain FIFO order, and content close drains pending records before closing files or releasing shaders. Queue exhaustion applies bounded backpressure instead of dropping cache entries; ordinary writes remain asynchronous.

- Shared Vulkan/D3D12 pipeline-description loading no longer slurps the cache into a vector. A C89 record reader borrows native mapped bytes or reuses a bounded 64 KiB VFS window. Hash validation and translation-key collection share one pass; only the validated prefix is repaired and replayed. Existing C++ descriptor objects are decoded one at a time, and queued pipeline jobs retain owned cache entries rather than borrowed file bytes.

- Removed Vulkan Memory Allocator and replaced its texture-cache image/buffer paths with a bespoke MSVC C89 block heap. Cold probes warm separate optimal-image and linear-resource blocks; suballocation metadata is reserved once, spans coalesce without array shifts, and generation-tagged handles reject stale frees. Required dedicated allocations, memory-type restrictions and bind-failure rollback are retained. Ordinary oversized blocks are reused and redundant empty blocks are released. Backing growth on exhaustion and required dedicated allocation still need further work toward allocation-free gameplay.

- Vulkan driver-cache loading now borrows an immutable native file mapping, retaining its owner through pipeline-cache creation. VFS backends without mappings use one exact C89 allocation with complete-read validation; partial blobs are discarded. Removed the load/save byte vectors and remaining stdio from this path; the driver serializes directly into its final save buffer.

- Shared Vulkan/D3D12 shader and pipeline persistence now uses libretro-common VFS for reads, writes, directories and checked repair/append positioning. Short and interrupted transfers are handled without an extra staging allocation. Shader records are checked against remaining file length before resizing microcode storage, and persistent endian encoding uses fixed 4 KiB C89 scratch instead of a growable vector. On-disk layouts and hashes remain unchanged.

- Native 48 kHz guest audio now converts byte order, channel layout and volume directly into owned queue spans using MSVC C89 code. This removes the intermediate converted block and queue copy, and folds volume into conversion. SSE/NEON stereo downmix retains the existing matrix and operation order. Non-48-kHz media conversion remains separate; whole-frame wrap publication and shutdown cancellation are preserved.

- Frontend audio batches now use fixed reusable storage sized for all supported channel and refresh modes. Float mixing writes directly into the final output buffer; integer conversion uses reusable storage, removing lazy integer allocation and per-run buffer growth.

- Render-target collection now marks ownership directly on cached objects, removing its temporary hash set and ownership-key copies in both Vulkan and D3D12. All four EDRAM ownership representations remain protected. Other gameplay allocation paths remain under audit.

- D3D12 now also retires orphan host render targets using the shared C89 submission queue and the same allocation-triggered collection policy as Vulkan. Resource and descriptor destruction waits for existing submission completion; CPU binding snapshots are invalidated. No extra GPU wait, readback or release logging is added.

- Paused guest CPU workers now park on a C89 libretro-common eventcount gate and wake directly on resume or shutdown, replacing repeated 2 ms sleeps. Guest time resumes before workers are released. Other scheduler timing and pause detection remain under audit.

- Vulkan now periodically retires cached render targets that no longer own EDRAM data, instead of retaining them until an explicit cache clear. Framebuffers retire before their image views after the last submitted GPU use; no device-idle wait, sleep or pixel copy is added. Render passes remain alive for asynchronous pipeline compilation. Active EDRAM owners are preserved, so this is not a hard cap on total VRAM.

- Removed standalone window/menu/surface handling, native dialogs and browser launch helpers, Vulkan/D3D12 swapchains and UI paint pipelines, immediate-mode drawers, profiler overlay and UI shader assets. Guest output remains available through frontend GPU handoff and capture. Removed the dormant Windows UI timing thread, Linux UI sleep loop, and per-refresh UI clock bookkeeping.

- Removed the host swap limiter and Frame Wait option, including translated entries. Guest vblank interrupts now consume frontend requests through a C89 libretro-common eventcount queue, with no independent frame timer or swap-polling sleep. Video/audio callback cadence is preserved.

- Fixed a content-close hang after socket use: socket I/O now stops and joins during kernel teardown, before DLL unload. Pending socket waits and selected guest events are retired while the kernel is alive; reopening content gets a fresh I/O context. The worker uses libretro-common threads instead of a process-static C++ thread.

- Replaced Boost.Context with owned libretro-common libco backends and a C89 lifecycle adapter. Fibers retain guarded stacks and restart in place without copying stack contents. Corrected current-fiber tracking after callback return and sanitizer cleanup for abandoned or migrated stacks; removed the Boost.Context dependency.

- Replaced pugixml/XPath in XLast metadata with owned libretro-common XML parsing and C89 indexes. Compressed XML decodes into parser storage; game metadata reads localized UTF-8 strings directly. Removed the obsolete dependency; differential tests retain their reference implementation in Git history.

- Replaced remaining TinySHA1 guest callers with C89 common SHA-1 adapters and removed its private header. Preserved guest state, alias ordering and final padded blocks; corrected the bit length above 512 MiB. Streamed HMAC now pads the whole block for short keys, retaining its 64-byte key cap. Added exact-state/RFC/513 MiB tests and measured hardware-dispatch/scalar benchmarks.

- Removed unused RapidJSON sources. XLast still uses pugixml while a common XML replacement is evaluated.

- Removed leaked FFmpeg contexts from streamed guest MD5/SHA-512 update and finalization. C89 adapters preserve guest count, word ordering, retained buffers and output aliases while borrowing bulk input. SHA-512 finalization now reads the correct 200-byte SHA-512 layout. Differential tests include retained-buffer behavior across 256 MiB MD5 update chunks.

- Guest SHA-256 now uses an allocation-free C89 adapter over common streaming primitives, preserving exact guest state, counter wrapping and aliasing behavior. Removed the private C++ SHA-256 implementation. One-shot guest MD5 no longer leaks an allocated context, and one-shot guest HMAC-SHA-1 shares the common span adapter. Fixed unaligned word loads in common MD5; streamed guest MD5/SHA-512 migration remains pending.

- Fixed an exit crash where a Windows APC could interrupt the logger join and let shutdown free its queue while the writer was still running. The writer is now joined non-alertably before its storage and sinks are destroyed.

- XEX block/image and title-update signature hashing now use libretro-common SHA-1. Account HMAC uses common streaming primitives through a bounded C89 span adapter, writing full digests directly to their destination. Account RC4 is now C89 with exact in-place support and checked key/output/overlap bounds; its unused key copy is removed. Guest-visible crypto state migration remains in progress.

- ZArchive writing now uses owned libretro-common streaming SHA-256 with inline state, preserving footer digests. Complete input blocks bypass staging copies; modern MSVC can use the runtime-checked SHA-NI path, with a bounded rolling scalar fallback. The private archive SHA implementation is removed.

- XEX metadata extraction now bounds optional-header counts, offsets and payloads before access. A shared allocation-free C89 parser reads only needed fields through common-backed file streams or archive reads, avoiding whole-header allocations and unaligned structure casts. Malformed metadata returns failure without publishing partial output.

- Replaced the C++ ZArchive reader with a direct C89 implementation. Immutable mapped metadata is borrowed in place; contiguous raw reads bypass the decoder/cache lock and intermediary block copies. The 64-block decoded cache is allocated lazily and uses fixed hash/LRU storage, keeping failed decodes private. Added checked metadata/tree validation and correct guest read-error propagation. Other filesystem paths and recursive device mounting remain in progress.

- ZArchive reads now use libretro-common streams, frontend VFS v2 when supported and native fallback. Mapped compressed blocks decode without an intermediary input copy; fallback scratch is allocated lazily and reused. Reads validate complete ranges, retry short/interrupted transfers and retain the cursor across adjacent requests. The shared archive cache/decoder uses rthreads instead of std::mutex. Fixed archive name-table endpoint bounds and two-byte name lengths; writer ownership now releases its child nodes. This starts the wider file-I/O migration; other loaders and filesystem APIs still need conversion.

- Removed the audio mixer's shared registry lock. A bounded C89 registry uses atomic read leases and deferred stream retirement, preserving queue ownership through concurrent creation, mixing and destruction. Cleanup stays off the frontend mix path, and readiness notifications are batched once per mix. The frontend now owns its C mixer for the loaded content instead of caching a replaceable C++ audio-system pointer; independently cancellable subsystem groups preserve that ownership across rebuilds and discard stopped audio tails. Audio setup precedes hardware callbacks, and failed-load retries release the previous registry after teardown. Upstream audio-system locks, host-deadline pumping and resampling remain in progress.

- Removed the extra core-side stick dead zone from guest XInput state, preserving small movements supplied by the frontend for the game to filter. Fixed Y inversion at -32768 and changed analog trigger conversion to nearest rounding at the required 8-bit boundary, recovering all 256 physical trigger levels from frontend-expanded values. Mapping lives in C89 code; digital trigger fallback is retained.

- Replaced the audio driver's overwrite-prone C++ ring with a C89 libretro-common SPSC queue. Unread samples remain consumer-owned, complete channel frames survive plain-ring wraps, and a logical 64 ms bound replaces the oversized buffer. Consumption notifications replace the 1 ms sleep loop; teardown cancels pending writes before existing joins. Mixing uses borrowed queue spans, compatible float submissions skip staging, and native-rate audio avoids the resampler call. Driver controls use common atomics; the bounded common-lock registry and remaining host-deadline/resampling paths still need further work. Media-player ownership is released before its audio system during shutdown.

- Fixed avoidable precision loss in legacy int16 audio output: use the frontend's 32768 normalization scale and nearest-even rounding instead of 32767 scaling with truncation. All 65,536 int16 values now survive a normalized-float round trip exactly; float-capable frontends retain native float output. Guest-visible XMA formats are unchanged.

- Audio now negotiates native float stereo or multichannel output at load, retaining classic int16 and per-sample fallback. The mixer writes into the final float buffer instead of its own intermediate vector; float-capable frontends avoid int16 quantization and widening. Float-only 5.1 interfaces are accepted, the output format stays fixed until unload, and negotiated callbacks are retired there. C89 boundary conversion handles nonfinite guest samples explicitly. Producer scheduling, resampling and queue ownership changes remain in progress.

- Linux builds expose the POSIX/GNU declarations needed by the selected libretro-common sources when compiling strict C. The definition is limited to that dependency target in both Make and CMake.

- Removed the standalone UI toolkit, widgets, bundled fonts and trace-viewer/dump executables. Guest UI requests retain headless answers and completion through the kernel dispatcher. A bounded C89 libretro-common queue delivers UI notifications from frontend progress, replacing detached UI threads and their sleeps. Guest tile/profile decoding and trace screenshots retain their independent image codecs. Other timing and threading migrations remain in progress.

- Replaced the remaining Zstandard library with owned libretro-common rzstd. ZArchive reads existing Zstandard-compressed blocks directly into its cache and lazily retains one decoder workspace per reader, avoiding per-block workspace allocation. Terminal matches use bounded word copies instead of copying their entire length byte by byte. Writing retains standard frames and the raw-block fallback; compressed sizes may differ. Removed the old source tree and unused standalone ZArchive build/packaging paths.

- Replaced the standalone xxHash and zlib-ng dependencies with selected owned RetroArch libretro-common sources. Existing XXH3 cache keys and Zstandard XXH64 checksums retain their values. Embedded patches decode into one allocation of their generated raw size; XLast rejects incomplete, corrupt or incorrectly sized gzip output. Native libretro-common atomic backends are selected explicitly; the wider threading and file-I/O migration remains in progress.

- Vulkan now negotiates context v2 and creates the shared device through the frontend's wrapper, retaining v1 fallback when support is absent or rejected. D3D12 negotiates the newer render interface, uses fenced sync-index handoff where available, and retains the legacy interface. New C89 D3D12 frame handling caches upload footprints and bounds teardown waits while retaining resources still in GPU use. Negotiation enables frontend device customization; it does not implement Reflex or DLSS.

- Removed miniaudio and the standalone achievement notification sound helper. Guest audio continues through libretro.

- Every frontend run now submits its full frame-rate-proportional audio duration, padding underruns or a missing mixer with silence. Muting still consumes queued audio to avoid accumulating latency, fractional sample counts reset with core initialization, and frontends with only the per-sample audio callback are supported.

- Vulkan negotiation no longer leaves a core device-destroy callback in RetroArch's cached video context. Non-owning Vulkan wrappers are released during context destruction/content close, while RetroArch retains and destroys its actual device even after the core DLL unloads. Added Vulkan and D3D12 lifecycle regressions that keep frontend devices alive past core unload; D3D12 registers no comparable late negotiation callback.

- Expanded the Windows content-close regression to reject any surviving thread whose entry point belongs to the core DLL, verify that the DLL really unloads, and document how to run the test without a ROM.

- Content close now flushes and joins Xenia's logging writer before the core can unload. Reset/context rebuild retains one logger; reopening content creates a fresh logger and frontend log sink. Repeated shutdown and late log messages safely handle an already stopped logger.

- Removed the Windows first-run disclaimer dialog, browser launch and Xenia registry acknowledgement check. Core startup no longer depends on a desktop prompt or the standalone emulator's registry state.

- Content close explicitly detaches the Windows fiber exception callback after stopping the emulator, including when retired guest frames retain fiber owners. This covers the repeated unload crash left by relying only on the last stack destructor; retained stack owners are reported in xenia.log.

- Closing content now unregisters the Windows fiber stack exception callback when the last fiber stack is released. Loading another title registers it again, preventing a stale callback into an unloaded core DLL and recursive access-violation stack dumps.

- Removed unused FFmpeg glslang/shaderc adapters and excluded them from upstream imports. Expanded native domain GPU checks to all 90 texture/export combinations. Fixed GCC ARM64 save/restore offsets and system-register reads, added POSIX core ABI/lifecycle checks, and clarified the D3D12 Shader Model 6.6 device requirement.

- Expanded native tessellation checks to execute memory exports alongside stream output, covering 60 GPU cases and 90 validated containers. Fixed shared host-tool signature checks during simultaneous builds. Android info-cache flag updates use the existing portable atomics, supporting libc++ releases without `atomic_ref`.

- Removed Mesa and its SPIR-V-to-DXIL fallback, source tree, build archives, Meson/Python setup and runtime DXIL validator DLL. D3D12 now generates guest and helper DXIL directly; Vulkan retains raw SPIR-V and macOS keeps its external driver. Windows and Linux CI use GCC with the static Makefiles. Sampler layouts now cover the renderer's 2,000-entry heap, and geometry cache publication keeps native bytecode without a copy. Added GCC cross-build portability fixes.

- Triangle/quad guest domain shaders now compile directly to DXIL and pair with the checked-in raw host vertex/hull helpers for discrete, continuous and adaptive tessellation. Matching patch signatures and a constant-buffer root alias avoid linked SPIR-V/Mesa conversion and bytecode copies. GPU stream-output checks cover coordinates, index layouts and clip/cull interfaces. Mesa fallback/dependency removal and platform checks remain.

- Guest vertex and pixel memory exports now compile directly to DXIL, including all existing export formats, extended half floats, endian modes, stream bounds and atomic subword writes. Executed writes mark only their eM elements dirty; ALLOC and true kills commit pending exports before resetting or suppressing later writes. Shared-memory reads during export draws use the UAV. Scaled pixel exports retain the center-host-pixel rule. Tessellation remains before Mesa removal.

- Scaled guest texture sampling now compiles directly to DXIL. Resolved textures use host-scaled sizes and offsets; point sampling preserves guest texel footprints until a coordinate write executes. Conditional and component writes update two compact validity masks instead of copying register buffers. Added 17 policy cases and 144 full ROV shader configurations, exercised in 864 GPU runs. Memory exports and tessellation remain before Mesa removal.

- Native DXIL now handles 3D and stacked-array guest sampling, runtime view selection, volume-filter overrides, mip/gradient policies and layer boundaries. Absolute layer gradients select minification correctly; zero-weight second-layer fetches are skipped. Computed-LOD queries use native unclamped DXIL operations across texture types and preserve signed-view selection, including query-only scaled draws. Vertex/explicit-gradient LOD queries and the unimplemented border-fraction query retain the core's zero-result policy without texture descriptors. Scaled sampling, memory exports and tessellation still require migration before Mesa removal.

- Native guest texture sampling now covers 1D (including wide textures stored as rows), promoted 1D/2D coordinates and all cube faces. Cube directions, face clamping/offsets, explicit or gradient LOD, and automatic LOD bias compile directly to DXIL. Wide-texture gradients account for the host row mapping and avoid an unrelated Y gradient changing mip selection. Texture metadata now reuses each constant-buffer vector and uses fixed lane extraction, avoiding repeated loads and dynamic lane selects. Added full pixel/vertex shader and GPU policy checks. 3D/stacked textures, remaining queries, scaled sampling, memory exports and tessellation still require migration before Mesa removal.

- Guest 2D texture fetches now compile directly to DXIL in vertex and pixel stages at unit draw resolution, with raw fetch-constant coordinates, explicit/gradient LOD, signed views, gamma and integer conversion. Uniform branches avoid unused sign samples; fetch metadata is reused. Native and SPIR-V variants retain one immutable descriptor layout under synchronized publication, and pipeline caches invalidate the previous layout ABI. GPU checks cover 30 sampling policies, 672 pixel draws, 648 textured ROV draws and textured vertex stream output. Other dimensions, scaled sampling, memory exports and tessellation remain to migrate before Mesa removal.

- Guest base-level filtering weights now generate native DXIL from raw fetch constants, covering 1D, 2D, stacked/3D and cube queries, normalized/unnormalized coordinates and half-texel offsets. Query-only vertex/pixel shaders bypass Mesa; pixel gradient queries use native derivatives. Added SM 6.6 descriptor-heap handles with bound-resource annotations and preserved shader-model selection for barycentrics. Native heap sampling passes GPU checks for 2D/array views, mips, point/linear samplers and runtime indices. Corrected mixed CBV/sampler PSV ordering. Empty descriptor-index tables reuse the existing zero upload, avoiding per-draw allocations and clears; unsupported sampling shaders are filtered before native IR construction. Full sampling policy, memory exports and tessellation still use Mesa.

- Guest pixel shaders now generate the ROV EDRAM epilogue directly in C89 DXIL: all twelve color formats, fifteen blend factors, five equations, masked/conditional outputs, scaled/wrapped addresses, per-sample depth/stencil and query counters. Full overwrites skip old-data reads, unchanged merges skip stores, and killed samples suppress writes while preserving early-test failure counts. Extended-half packing rounds explicitly rather than using the truncating/saturating legacy DXIL conversion. Signed fixed-point unpacking preserves the full [-32,32] range in both native DXIL and Vulkan. The host premultiplication adapter now uses the actual Xbox source-color enums 4/5, checked against the renderer enum. Textures, memory exports and tessellation still require Mesa.

- Host render-target guest pixel shaders now compile directly from microcode to DXIL in live draws, async pipeline creation and cache warm-up. The C89 adapter handles sparse/centroid/barycentric inputs, generated pixel/point parameters, deferred ALU kills, conditional alpha tests and coverage, gamma/exponent adjustment, blend premultiplication, all seven depth modes and query totals. Disabled alpha work and unused depth derivatives are skipped, and cached containers avoid temporary bytecode copies. Native cache readiness also prevents repeat interpreter/skip decisions for an already compiled vertex shader. Native and remaining Mesa stages now share fixed linkage registers, including packed clip/cull distances; the pipeline cache version changes with that ABI. Polygon-offset depth uses ordinary depth output so positive offsets are preserved. Textures, memory exports, ROV guest color and tessellation still require migration before Mesa can be removed.

- Plain guest vertex shaders now compile directly from Xbox microcode to native DXIL in the D3D12 renderer and shader-cache warm-up. The C89 adapter handles index endian/base conversion, guest position conventions, sparse interpolators, clip/cull distances, point size and vertex killing. Cached containers pass directly to pipeline creation without copying their bytecode. Six containers validate unchanged and 210 WARP executions pass. Textured/memory-export vertex shaders, pixel shaders and tessellation still require migration before Mesa can be removed.

- Added the native C89 guest-program layer joining raw microcode control flow, ALU, full/mini vertex fetch and texture-state dispatch with real constant-buffer bindings. Relative accesses clamp to declared storage, static float constants retain tight packing, and export tracking follows executed writes. Export preparation initializes only referenced slots; two sample containers are roughly half their previous size. Nine WARP fixtures pass 288 exact words. Renderer stage adapters and Mesa removal remain unfinished.

- ROV depth-only and VIZ survey shaders now generate native DXIL, replacing their Mesa conversions. Ordered EDRAM access, tiled/scaled sample addresses, per-face polygon offset, depth/stencil tests and query counters are handled in owned C89 code. Uncovered samples skip EDRAM access, unchanged words skip stores, and only the active depth format is converted. Native containers stay owned directly through pipeline creation. Guest shader and tessellation translation still require Mesa.

- Host-depth pixel shaders now generate native DXIL for empty rasterization, float24 truncation/rounding and hybrid occlusion coverage counting. Truncation preserves conservative depth output for early depth testing. Pipeline initialization keeps the native buffers directly and releases them after shader workers join; obsolete prebuilt float24 shaders were removed. WARP checks cover 1x/2x/4x MSAA, disabled/nonzero counter slots and float24 underflow, tie and clamping boundaries. Guest, ROV depth/stencil and tessellation shaders still need migration before Mesa can be removed.

- D3D12 EDRAM ownership-transfer shaders now generate native DXIL for all ten modes, including color/depth reinterpretation, stencil-bit passes and host-depth preservation. Compatible transfers avoid pack/unpack conversions and pipeline creation uses the native container directly. Container checks cover MSAA, native/scaled layouts and all color-format pairings; four WARP executions verify exact color preservation and scale remapping. Guest and tessellation stages still require migration before Mesa can be removed.

- D3D12 direct resolves now generate DXIL directly, including scaled layouts, array destinations, byte-order changes, red/blue swapping and half-pixel fill. All 168 variants validate; 64 WARP executions cover 32bpp and 64bpp output. Dump/resolve pipeline creation consumes the native container without copying it through a temporary vector. Native pixel-interface metadata for ownership transfer also passes five validator and WARP pipeline checks. Ownership transfer and guest/tessellation paths still need migration before Mesa removal is complete.

- D3D12 primitive expansion now generates DXIL directly for points, rectangles, quads and scaled lines, without SPIR-V or Mesa conversion. WARP execution checks cover attributes, clipping/culling signatures and primitive discard. Other shader paths still require replacement before Mesa can be removed.
- Standard D3D12 render-target dumps now emit DXIL directly. All 168 color/depth, MSAA and scaling variants pass validation; a WARP dump checks 128 words. Shared native EDRAM conversions pass 8,576 bit-exact words, including every 7e3 encoding and depth rounding boundaries. Direct resolves and ownership transfers remain to be replaced.
- The native C89 guest texture frontend now handles persistent LOD/gradients, predication, relative register addressing, destination swizzles and pixel derivatives. Raw instruction execution checks pass 168 words.

- Native Windows Make builds now use MinGW-w64 GCC and GNU binutils by default, without LLVM or Clang. The compiler runtimes are linked statically; the optional MSVC-ABI cross build remains available.
- MinGW builds use ABI-safe D3D12 calls and the owned GNU context-switch assembly. Title exit requests frontend shutdown instead of ending RetroArch. Timer workers start lazily and are joined during deinitialization so the DLL can unload safely.

- Native C89 DXIL texture primitives decode guest packets and emit level, gradient, implicit and biased samples. A two-mip 2D texture passes 88-word WARP execution checks; pixel-stage implicit/bias samples pass validation. Guest texture policies and live renderer integration remain unfinished.

- GNU Make now detects native Windows shells correctly, selects the DLL target, and names the host generator with .exe. Missing target compilers stop the build early with an actionable message; host-only tools and checks remain available.

- The native C89 DXIL frontend can read raw GPU buffers and dynamically indexed constant-buffer words. A raw guest vertex-fetch program passes execution checks with actual D3D12 SRV/CBV bindings; invalid reads branch around memory access. Live renderer integration remains unfinished.

- Native C89 DXIL vertex-fetch lowering now decodes raw instructions, converts all 15 declared vertex formats, and handles full/mini address state, bounds and predication. A 472-word D3D12 execution check passes. Texture fetches, live resource bindings and renderer integration remain unfinished.

- The independent C89 DXIL frontend now lowers guest calls, returns, loops, jumps and conditional endings. Five native shaders pass validator and D3D12 execution checks; real fetch/resource bindings and renderer integration remain unfinished.

- The native DXIL ALU frontend decodes Xbox instructions and lowers all 30 vector and 50 scalar opcodes in C89. Validator and D3D12 execution checks cover co-issue, predication, relative addressing and export masks. Whole-shader integration is still in progress.
- macOS remains supported with an external Vulkan driver; MoltenVK is no longer bundled.
- Dependencies are ordinary, editable source files. Native C89 build tools embed the checked-in fonts, patch data and raw built-in shader binaries.
- The independent raw DXIL writer has validator and D3D12 execution checks for arithmetic, control flow and UAV stores. Guest shader integration remains in progress.
- `make clean` no longer needs Python; it checks and removes the build directory with shell tools.

- The core is named `parallel_xbox360`, including its binaries and frontend metadata. Existing `system/Xenia-Edge/` data and core option keys are retained.
- A direct GNU Make build supports all five GitLab CI platforms without CMake.

- Core info: no longer marked experimental (NNshi).
- Linux: the core exports only the libretro API (a version script); it exported some sixteen thousand of its own symbols, which could clash with libraries the frontend loads.
- Music Player Volume is 70% by default, as in standalone Xenia Edge; at 100% the music of a game that does not set its own volume could clip (NNshi).
- Emulation Speed is gone: it scaled Xenia's clock, but the game's frames come from RetroArch, so games did not get faster, and slower made the sound break up; RetroArch's Fast-Forward and Slow-Motion change the speed. License Mask is Full by default, so Xbox Live Arcade games run as their full version, not their trial. Frame Wait's description says it can make a game stutter, not slow down (NNshi).
- Core options: every option that takes effect only after Reset, or after the content is loaded again, says "(Restart)" in the name RetroArch shows; most said nothing, and the five that were marked had the mark only in the name RetroArch shows without categories. Mute Audio and Fuzzy Alpha Epsilon are gone: neither did anything (Xenia Edge no longer reads either setting in the core; RetroArch has its own mute, and Audio Output turns the sound off). The Debug category is gone too, and its one option is now Emulation > Log Level (NNshi).
- Wait for the Game's Frame is now called [Libretro] Frame Wait, which says that it is the core's and not a setting of the standalone. Its saved value carries over (NNshi).
- A full dump of an original Xbox disc (a Redump image, with the game partition at 0x18300000) is recognised as a disc image; only images rebuilt to hold the game partition alone were. Original Xbox games run through XeFu, the console's own emulator, as in upstream. No one has opened a full dump with it yet.
- Video > Widescreen is now Aspect Ratio, 16:9 (default) or 4:3, and with 4:3 the picture is shown at 4:3: RetroArch was told 16:9 whatever the option said, so a game with a native 4:3 mode came out stretched. A change while a game runs is passed on at once (NNshi).
- Dialog Notifications (Emulation): the notification that shows a system dialog's title and the answer the core gave can be turned off; it stays on by default (NNshi).
- A game opening the achievements list, the gamer card or the profile editor no longer crashes the core: those dialogs have no window to be shown in and are dismissed at once (NNshi, Dead or Alive 5 Ultimate's Achievements menu).
- Linux: the core no longer links libX11 and libxcb, and builds without the X11 and Wayland headers. They came with the standalone's Vulkan surface creation, which the core never does: the frontend owns the window. The core now loads where those libraries are absent.
- The libretro buildbot builds the Windows core too: its MSVC is too old for Xenia Edge, so the job cross-builds on Linux with clang-cl against the MSVC runtime and Windows SDK (`.ci/build-windows-cross.sh`). A GitHub workflow replays the job. The Windows builds from GitHub Actions are still made with MSVC.
- The core option texts can be translated through libretro's Crowdin project: the options header is on the standard template, with the `intl/` scripts and the upload and sync workflows. All texts are English until the first sync.

## 2026-10-10

### Video

- The Vulkan device is shared with RetroArch: the core makes it through RetroArch's context negotiation, Xenia renders on it and each frame goes GPU to GPU into RetroArch's image in the same frame, with no readback and upload. GPU load is on par with standalone.
- Wait for the Game's Frame (Graphics, off by default): removes one frame of input lag at the possible cost of the game slowing down.
- One guest vblank per retro_run, so frames are no longer doubled by two 60 Hz clocks drifting apart; the guest pauses when RetroArch stops running the core.
- The largest picture RetroArch is told to expect follows Draw Resolution Scale.
- Turnip (Adreno): frames are no longer black below their top 16 rows.

### Audio

- RetroArch gets one frame of audio per run, and the guest's audio waits once 64 ms is queued.
- Audio clients and the media player are mixed, each in its own format and rate; menu music is no longer static. An Audio Channels option (Surround 5.1).

### Games and data

- Per-game data: Unlock Framerate from NNshi's frame rate unlock list.
- Multi-disc games: an `.m3u` list and RetroArch's Disc Control, with the game's request for a disc answered from the list.
- Core options for the settings that game-optimized configurations use, and a Patch Manager; every option's value is logged at load.
- The standard drives are mounted as the standalone does; a missing `cache:` crashed Forza Horizon 2 at its first race.
- Keystrokes from the RetroPad, for titles that run their menus on them.

### Without a window

- Message boxes are answered the way a player would go on: an offline choice first, else the game's own default when it is not a way back, else the first button that is not Cancel, Back or No. What was asked and answered goes to the log and on screen.
- The keyboard and sign-in dialogs are answered too; the profile's name comes from `system/Xenia-Edge/xbl_profilename.txt`.
- No crash at an achievement popup.

### Other

- The core's version names upstream's commit and this repository's.
- lz4 is linked only where it is installed.

## 2026-10-08

- Xenia's log goes to `system/Xenia-Edge/xenia.log`, and its errors and warnings to RetroArch's log as well; a crash names the module it happened in.
- Everything the core keeps is in `system/Xenia-Edge/` (content, cache, D3D12), nothing beside RetroArch's executable. A core info file.
- Games no longer start to a black screen: graphics, audio and input are set up before the title starts.
- A Graphics API option: Vulkan, and on Windows Direct3D 12, shown only where it can be used. `dxil.dll` is carried inside the core.
- Games see the controllers: RetroPad port N is user N.
- Closing content stops the title's threads in order and no longer takes RetroArch with it.
- A profile is signed in for games that ask for a signed-in user, and made when there is none.

## 2026-10-07

- The core builds from upstream's CMake build, at upstream Xenia Edge 8dc77b0, and reports upstream's version. The repository holds the core only: the standalone app, its UI and packaging are removed (`.upstream-excluded`).
- Upstream's XAM dialogs are answered at once where there is no window.
- Linux ARM64 and Android builds, next to Linux x86_64 and Windows; macOS on Apple Silicon.

## February 2026

- libretro core integration, by danprice142: the original port this fork builds on.
