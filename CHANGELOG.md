# Changelog of the libretro core

What changed in the libretro core of this fork, newest first. Changes that come with a merge of upstream Xenia Edge are in upstream's own history; a merge is noted here with the upstream commit it brought in.

## Unreleased

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
