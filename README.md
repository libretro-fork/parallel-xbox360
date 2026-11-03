# parallel_xbox360 libretro core

A [libretro](https://www.libretro.com/) core of [Xenia Edge](https://github.com/has207/xenia-edge), the Xbox 360 emulator, for RetroArch and other libretro frontends.

The emulator is upstream's: this repository follows xenia-edge and adds the libretro frontend in `libretro/`. It holds the core only; upstream's standalone UI, packaging and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is merged to (`upstream.version`). The core is currently on xenia-edge commit 8dc77b0 of 7 October 2026.

## Downloads

The core is built by libretro's buildbot for Windows, Linux (x86_64, arm64) and Android (arm64-v8a, x86_64), and is installed from RetroArch: Online Updater > Core Downloader. Every push to the `libretro` branch is also built in [Actions](https://github.com/libretro-fork/parallel-xbox360/actions/workflows/libretro.yml), macOS (Apple Silicon) included; the builds are the artifacts of a run, and downloading them needs a GitHub login.

- **Windows:** `parallel_xbox360_libretro.dll`, whose MSVC builds need the Visual C++ 2015-2022 runtime (GNU Make/MinGW builds link their compiler runtimes statically).
- **Linux:** `parallel_xbox360_libretro.so`, glibc 2.35 or newer; uses fontconfig and the C/C++ runtime, with no X11 or Wayland libraries (the CMake build also links lz4 where available). ARM64 uses the a64 CPU backend.
- **macOS:** `parallel_xbox360_libretro.dylib`, Apple Silicon only. The x64 CPU backend needs the address space below 4 GB, which a core loaded into RetroArch cannot free, so there is no Intel build.
- **Android:** `parallel_xbox360_libretro_android.so`.

## Video

The core option Graphics API picks Xenia's GPU backend: Vulkan, or on Windows Direct3D 12. The core asks RetroArch for that kind of context, and RetroArch switches its video driver to match when it is allowed to (Settings > Video > Output). Direct3D 12 runs on the system's Direct3D 12 runtime: the Agility SDK runtime that standalone Xenia uses can only be loaded by an executable that exports it, which RetroArch does not.

## Content

Xbox 360 games as a disc image (`.iso`), an executable (`.xex`), or a `.zar` / `.xcp` package. No BIOS or firmware is needed.

Original Xbox games run through XeFu, the Xbox 360's own emulator of the original Xbox, which upstream Xenia Edge can run. It needs XeFu's files from a console (`xbox.xex` and the `xefu*.xex` builds of the system partition's `Compatibility` folder) in `<system>/Xenia-Edge/xefu/`; the game is opened as a disc image (`.iso`), a full dump or one rebuilt to the game partition alone. Only the games the console itself could run have a chance.

Everything the core keeps is in `system/Xenia-Edge/`: saves, title updates and DLC under `content/`, laid out as in standalone Xenia's content folder; the shader and other caches under `cache/`; and Xenia's log, `xenia.log`. Errors and warnings from it also appear in RetroArch's log. The core option Apply Title Updates turns updates off.

Games that need a signed-in profile get one: the core signs in the first profile under `content/`, or creates one when there is none, named after the first line of `system/Xenia-Edge/xbl_profilename.txt` ("Player" if the file is missing, which the core then writes there).

## Building

The checked-in `Makefile` and `Makefile.common` compile the core and its
owned dependency sources directly, without CMake. Use GNU Make, a C++20 target compiler and a host C compiler.
Linux also needs the fontconfig
development package. Fonts, patch data and raw SPIR-V/DXIL built-in shaders
are included in the repository; a native C89 tool embeds them during the build.
Linux, Android and macOS builds do not require Python or a shader compiler.
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
cube coordinates convert directly to native directions. Resource layouts are published once and retained across native and
remaining SPIR-V variants. Only required sign views are sampled, and fetch
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
[`tools/tests/README.md`](tools/tests/README.md); whole-shader integration
is still in progress. The native program layer joins raw control flow, ALU and
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
