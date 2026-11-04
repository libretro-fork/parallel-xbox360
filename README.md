# parallel_xbox360 libretro core

A [libretro](https://www.libretro.com/) core of [Xenia Edge](https://github.com/has207/xenia-edge), the Xbox 360 emulator, for RetroArch and other libretro frontends.

This repository follows xenia-edge and adds the libretro frontend in `libretro/` plus owned shader and build paths. It holds the core only; upstream's standalone UI, packaging and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is merged to (`upstream.version`). The core is currently on xenia-edge commit 8dc77b0 of 7 October 2026.

## Downloads

The buildbot configuration covers Windows, Linux (x86_64, arm64) and Android (arm64-v8a, x86_64). The [Actions workflow](https://github.com/libretro-fork/parallel-xbox360/actions/workflows/libretro.yml) also includes macOS on Apple Silicon. Successful runs publish build artifacts when Actions is enabled.

- **Windows:** `parallel_xbox360_libretro.dll`, whose MSVC builds need the Visual C++ 2015-2022 runtime (GNU Make/MinGW builds link their compiler runtimes statically).
- **Linux:** `parallel_xbox360_libretro.so`, glibc 2.35 or newer; uses the C/C++ runtime, with no X11 or Wayland libraries (the CMake build also links lz4 where available). ARM64 uses the a64 CPU backend.
- **macOS:** `parallel_xbox360_libretro.dylib`, Apple Silicon only. The x64 CPU backend needs the address space below 4 GB, which a core loaded into RetroArch cannot free, so there is no Intel build.
- **Android:** `parallel_xbox360_libretro_android.so`.

## Video

The core option Graphics API picks Xenia's GPU backend: Vulkan, or on Windows Direct3D 12. The core asks RetroArch for that kind of context, and RetroArch switches its video driver to match when it is allowed to (Settings > Video > Output). Direct3D 12 runs on the system's Direct3D 12 runtime: the Agility SDK runtime that standalone Xenia uses can only be loaded by an executable that exports it, which RetroArch does not. D3D12 requires Shader Model 6.6 support from the active runtime and graphics driver for native descriptor-heap indexing.

## Content

Analog sticks preserve the frontend's raw 16-bit values; the core adds no stick dead zone before the title reads XInput state. Configure frontend dead zones as desired; games retain their own filtering. Y inversion saturates its unrepresentable positive endpoint, and analog triggers round once to the Xbox controller's 8-bit format, preserving physical 8-bit levels through frontend expansion. This follows [XInput's application-side dead-zone policy](https://learn.microsoft.com/en-us/windows/win32/xinput/getting-started-with-xinput).

Startup does not display the standalone Windows disclaimer dialog or read/write its registry acknowledgement.

Guest UI requests complete headlessly: message boxes choose an offline or confirmation answer, keyboards retain their default text, and storage selection uses the first available hard disk. UI open/close notifications use a bounded libretro-common queue drained by `retro_run`; the close delay follows frontend progress. Standalone windows, menus, native dialogs, swapchains, UI scaling/overlay pipelines, immediate-mode drawers and their shader assets are removed. The presenter only maintains guest output for frontend GPU handoff and capture. No standalone UI timing thread or sleep loop is created. There are no bundled UI fonts or fontconfig dependency. Guest profile/tile image decoding remains available independently of the UI.

Xbox 360 games as a disc image (`.iso`), an executable (`.xex`), or a `.zar` / `.xcp` package. No BIOS or firmware is needed.

Original Xbox games run through XeFu, the Xbox 360's own emulator of the original Xbox, which upstream Xenia Edge can run. It needs XeFu's files from a console (`xbox.xex` and the `xefu*.xex` builds of the system partition's `Compatibility` folder) in `<system>/Xenia-Edge/xefu/`; the game is opened as a disc image (`.iso`), a full dump or one rebuilt to the game partition alone. Only the games the console itself could run have a chance.

Everything the core keeps is in `system/Xenia-Edge/`: saves, title updates and DLC under `content/`, laid out as in standalone Xenia's content folder; the shader and other caches under `cache/`; and Xenia's log, `xenia.log`. Errors and warnings from it also appear in RetroArch's log. The core option Apply Title Updates turns updates off.

Fibers use owned libretro-common libco AMD64/ARM64 backends, including direct AMD64 switching under MSVC. A C89 lifecycle adapter reuses guarded stack allocations on restart without copying stack contents. On Windows, the fiber stack exception callback is removed when the last stack is released and explicitly detached after emulator shutdown, including when retired guest frames retain stack owners. Any retained owners are reported in `xenia.log`.

Content close flushes and joins the logging writer non-alertably before the core DLL can unload. Windows APC delivery cannot release its queue and sinks before the writer exits. Reset and graphics context rebuild share the existing logger; reopening content starts it again.

The kernel owns socket I/O. Closing content cancels live socket waits, joins the I/O worker and releases pending handlers before guest state is destroyed. Closed wrappers retained by retired guest frames keep no running worker or pending I/O context. No socket worker is joined from a process-static DLL destructor.

Synchronous guest I/O events are recycled only after both the waiting caller and signaling worker retire. A C89 common-atomic pair prevents a late completion signal from reaching a reused event; its state is confined to private host I/O events. Offloaded calls carry no host queue/run timestamps or I/O diagnostic counters. The existing guest I/O queues, call allocations and worker-pool growth remain to be replaced.

Each `retro_run` supplies one video callback and one run's audio duration at the declared refresh rate. Fractional sample counts carry forward; audio underruns and a missing mixer produce silence for the remainder. Muting drains audio without adding a backlog. Stereo frontends may use either the batch or per-sample callback.

Vulkan and D3D12 collect orphan host render targets above 512 MiB after new allocations, with 120 guest frames between collections. This threshold controls reclamation attempts, not a hard VRAM cap: active EDRAM owners and resources still used by GPU submissions remain alive. Ownership collection marks existing targets directly instead of allocating a temporary ownership set. Resource retirement uses a C89 queue without an extra GPU wait. Vulkan framebuffers retire before images and render passes remain cached for async pipeline jobs; D3D12 descriptors stay alive with their resources until submission completion. This addresses a source of runtime retention; the Tekken Tag Tournament 2 slowdown still needs a gameplay retest.

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

Frontend audio mixing and integer conversion use fixed reusable per-run storage, with no lazy allocation or buffer growth in retro_run. Audio queues use owned libretro-common SPSC spans with a logical 64 ms bound per driver. The producer waits for consumption notifications when full and never overwrites unread samples. Native 48 kHz production converts directly into queue spans, without a temporary converted block or its copy into the ring; volume is applied during conversion. A plain non-mirrored ring needs at most one six-channel frame of wrap scratch. Mixing reads queue spans directly, avoiding a pop-to-scratch copy. Borrowed guest buffers still require queue-owned storage before guest reuse; this is not zero-copy across the entire audio path. Teardown cancels pending writes before existing worker joins, including subsystem rebuilds. Driver controls and the bounded C89 registry use native common atomics. Per-stream read leases protect retirement without a shared mixer lock; retired buffers are collected during cold creation or teardown, and readiness notifications are batched once per mix. The frontend owns the registry for the loaded content; independently cancellable subsystem groups let rebuilds replace the C++ audio system without invalidating the frontend mixer or replaying stopped audio tails. Host-deadline pumping, resampling and the upstream audio-system synchronization remain unfinished.

The ZArchive reader is now C89 code using owned libretro-common streams/VFS and rthreads, with direct C calls from the core. Native immutable mappings lend metadata and compressed input without copying or endian-converting whole tables. Validated contiguous raw spans copy directly to the final destination without a decoder/cache lock. The bounded 64-block decoded cache allocates its 4 MiB payload only when needed, avoids per-block container allocations and publishes only complete, successfully validated blocks. Frontend VFS and unsupported-mapping paths retain exact reads and bounded reusable scratch. Reader operations must be quiesced before close.

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

## License

BSD, as Xenia. See [LICENSE](LICENSE).
