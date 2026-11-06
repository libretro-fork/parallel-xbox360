# Remaining audio optimization work

This is part of the active core optimization goal. These are audit and
implementation candidates, not completed changes or permission to alter the
known-good audio schedule. Preserve consecutive per-run delivery, fractional
sample accounting, mixing, guest-visible decoder behavior and teardown ordering.

## Current baseline

- Native 48 kHz production already converts directly into owned
  libretro-common SPSC write spans. Mixing reads those spans directly. A borrowed
  guest source cannot outlive the guest's right to reuse it.
- `retro_spsc_t` already separates producer and consumer cursor groups with
  cache-line padding, including padding after the consumer group. It also caches
  remote cursors. Do not add a second span registry merely to duplicate this.
- Native channel conversion already has SSE/NEON paths. Legacy float-to-int16
  output has explicit saturation and nearest-even rounding; wider SIMD must
  preserve its numeric contract.
- Media decoding still has a PCM vector in `audio_media_player.cc`. Non-native
  rates still pass through `LibretroAudioDriver::Resample` and scratch storage.
- XMA contexts retain decoded frames, PCM conversion and guest output-ring
  writes. The vendored XMA2 decoder advertises DR1 and delayed output; this makes
  a custom allocator worth investigating, not automatically safe.

## Decode storage and fused transforms

1. Trace allocation counts, frame ownership and peak simultaneously retained
   frames in all XMA modes and media codecs. Distinguish steady-state decoder
   reuse from real per-frame allocation before replacing it.
2. Evaluate a bounded, reusable, aligned C89 decode pool through FFmpeg's
   `get_buffer2` contract. Respect codec capabilities, per-plane alignment,
   linesizes, reference-counted AVBuffer ownership, retained/delayed frames,
   flush/reset, allocation failure and concurrent contexts. Never recycle a
   leased slot. Size from proven bounds; do not guess a small circular depth or
   add an extra playback queue.
3. Replace media PCM vectors with bounded/reusable storage. Fuse channel mapping
   and volume into the final writable destination where possible. Prefer direct
   destination writes over a forced in-place transform: planar/interleaved
   overlap, expansion and shared decoder frames can make in-place writes unsafe.
4. Investigate converting XMA PCM directly into writable guest-ring segments,
   removing host staging copies while preserving skip/padding/subframe rules,
   integer/endian semantics and guest memory visibility. The guest ring itself
   is emulated state and cannot simply be bypassed for frontend playback.
5. Audit non-native media sample-rate conversion separately for guest API
   semantics. Do not add frontend resampling or time stretching under the name
   of optimization. Preserve the working schedule while this is established.

## Handoff and SIMD

1. Check actual cursor/control layout on MSVC, GCC and ARM, including embedding,
   cache-line boundaries and the single-producer contract. Measure contention
   before changing the existing common queue. Use C89 padding/alignment helpers,
   not C++ `alignas`; payload alignment does not follow from cursor padding.
2. Benchmark existing scalar/SSE/NEON conversion and mixing against optional
   wider kernels. AVX2 requires supported-host dispatch and a compatible fallback.
   Prove alignment for each leased span or use unaligned loads, and handle tails
   without overread. Preserve channel order, coefficients, operation order where
   required, NaN/infinity behavior, clipping and rounding. Keep float samples
   float for a float frontend; quantize only at required output boundaries.
3. Measure complete transforms, including memory traffic and dispatch overhead.
   SIMD performs multiple lanes per instruction; an entire mix/convert/clamp is
   neither one instruction nor zero latency. Require measured improvement.

Offline gates include allocation/copy counts, wrap/partial-span tests, retained
frame and teardown stress, byte/sample comparison across XMA modes, numeric
boundary tests and sanitizers. Any scheduling or sample-production change also
requires sustained gameplay audio validation; FMV-only testing is insufficient.
Use test-only instrumentation and preserve release hot-path overhead.
