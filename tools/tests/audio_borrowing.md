# Borrowed audio handoff regression tests

Run `tools/tests/run_direct_audio_tests.sh` with a C compiler. Linux builds use
AddressSanitizer and UndefinedBehaviorSanitizer by default. Set `SANITIZER=` to
disable them for toolchains that do not provide them.

On Windows, run `tools\tests\run_direct_audio_tests.cmd` from an x64 MSVC
developer prompt after building the core. Its optional first argument selects
the CMake build directory. These tests compile as C with `/W4 /WX`.

The source owns each submitted sample block until the synchronous borrowed
submission returns. The frontend mixes directly from that block into its final
float batch. Native planar big-endian samples are converted, mixed and scaled in
that pass. Registered 48-kHz media sources use the same borrowed transport, while
their decoder-side sample preparation is still separate.

Borrowed ports allocate no sample ring. Registration allocates descriptors on
the cold path. A single reusable registry carry holds the remainder of a native
256-frame quantum when the frontend interval does not divide evenly by 256.
Completing the quantum lets a shared guest worker continue to its other ports;
holding its submission across retro_run instead could deadlock those ports.
This carry is bounded, rather than a target fill level or host pacing queue.
Float callbacks receive the mix storage itself. The standard libretro int16
callback requires one float-to-int16 conversion into its final callback buffer.

The tests cover:

- Borrowed pointer identity, partial reads and cancellation fencing an active
  reader before producer storage can be reused.
- Exact conversion and accumulation for every span size from 1 through 257,
  stereo/surround output, channel matrices, gains and destination bounds.
- 15,000 frontend intervals per queue backend, alternating 800, 960 and 400
  frames, with two sources and different native/media packet lengths.
- Immediate producer sample poisoning after submission returns, to catch reads
  after the lifetime guarantee ends. Sources must remain consecutive and mixed,
  with no initial prefill or unsolicited production.
- Cancellation of producers blocked on frontend demand at shutdown.

These offline tests validate transport and sample continuity. They do not prove
that an entire game's guest audio graph is scheduled correctly.
