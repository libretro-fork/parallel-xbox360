# parallel_xbox360 libretro core

A [libretro](https://www.libretro.com/) core of [Xenia Edge](https://github.com/has207/xenia-edge), the Xbox 360 emulator, for RetroArch and other libretro frontends.

The emulator is upstream's: this repository follows xenia-edge and adds the libretro frontend in `libretro/`. It holds the core only; upstream's standalone UI, packaging and CI are not part of it (the paths are listed in `.upstream-excluded`). The version the core reports is upstream's, with the upstream commit it is merged to (`upstream.version`). The core is currently on xenia-edge commit 8dc77b0 of 7 October 2026.

## Downloads

The core is built by libretro's buildbot for Windows, Linux (x86_64, arm64) and Android (arm64-v8a, x86_64), and is installed from RetroArch: Online Updater > Core Downloader. Every push to the `libretro` branch is also built in [Actions](https://github.com/WizzardSK/xenia-libretro/actions/workflows/libretro.yml), macOS (Apple Silicon) included; the builds are the artifacts of a run, and downloading them needs a GitHub login.

- **Windows:** `parallel_xbox360_libretro.dll`, which needs the Visual C++ 2015-2022 runtime. It carries `dxil.dll`, Direct3D 12's shader validator, inside and writes it to `system/Xenia-Edge/D3D12/` on start.
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
vendored dependencies directly, without CMake. Use a Linux build host with
GNU Make, Python 3, clang 21, GCC 14's libstdc++, and the submodules initialized.
Linux also needs the fontconfig development package. Slang and game patch data
are fetched before building (there are no downloads at runtime):

```sh
git clone --recursive https://github.com/libretro-fork/parallel-xbox360.git
cd parallel-xbox360
./xenia-build.py slang
./xenia-build.py fetchdata
make -j8 platform=linux-x86_64 CC=clang-21 CXX=clang++-21 HOST_CXX=clang++-21 AR=llvm-ar-21
```

Every platform in `.gitlab-ci.yml` has an explicit selection:

| `platform` | Output | Requirements |
| --- | --- | --- |
| `linux-x86_64` | `parallel_xbox360_libretro.so` | Native x86_64 Linux toolchain |
| `linux-aarch64` | `parallel_xbox360_libretro.so` | Native aarch64 Linux toolchain, or `CC`/`CXX` cross compilers with a target sysroot |
| `windows-x86_64` | `parallel_xbox360_libretro.dll` | LLVM, MSVC CRT/SDK, Meson, Ninja, Mesa and DXC; see `.ci/build-windows-cross.sh` |
| `android-arm64-v8a` | `parallel_xbox360_libretro_android.so` | `NDK_ROOT=/path/to/ndk` |
| `android-x86_64` | `parallel_xbox360_libretro_android.so` | `NDK_ROOT=/path/to/ndk` |

`platform=unix` selects the native Linux architecture. `unix-arm64` and
`windows` are aliases. Android defaults to API 28 and links the static C++
runtime with 16 KB page alignment. `CC` and `CXX` override the target compilers;
`HOST_CXX` always compiles build tools for the Linux host. `AR` selects LLVM's
archiver (the NDK's `llvm-ar` on Android, `llvm-ar` otherwise). A prebuilt matching
`xenia-shader-cc` may be supplied with `HOST_SHADER_CC=/absolute/path/to/tool`.
Set `SLANGC_PATH` if Slang is outside `.slang/`.

Build products are isolated under `build/make/<platform>/`; the final core is
written to the repository root. `BUILD_DIR` and `HOST_DIR` override the build
directories. `DEBUG=1` enables a debug build; `LTO=1` enables clang ThinLTO.
`make clean platform=...` removes that target's build files and root artifact;
shared host tools remain for other targets. `make help` lists the selections.
The fixed zlib-ng configuration uses its portable C implementation.

The CMake build remains available through `xenia-build.py` and produces the
same renamed core. macOS remains available through that build. Existing
`system/Xenia-Edge/` content and `xenia_*` option keys are retained.

The CI workflow `.github/workflows/libretro.yml` builds every platform, including the Android cross build.

## License

BSD, as Xenia. See [LICENSE](LICENSE).
