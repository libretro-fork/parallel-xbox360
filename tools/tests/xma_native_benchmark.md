# Native XMA offline comparison

`xma_native_benchmark.c` links the same raw decoder sources as the core and its
existing FFmpeg XMAFRAMES library. It does not run a game or access the frontend.
This is a decoder throughput comparison, not an audio-cadence or gameplay test.

Input is a private sequence of records: three little-endian uint32 fields
(payload byte length, leading bit offset 0..7, frame bit length), followed by
that many payload bytes. Each file is one consecutive 48-kHz mono/stereo stream,
maximum 1024 frames. Do not publish extracted game data. Pass the filename and
channel count as arguments. Missing/empty/invalid input fails the run.

Before timing, every decoded frame is compared with FFmpeg. Both decoders and
all compressed records/FFmpeg AVPackets are prepared before timing. The native
path borrows compressed bytes; FFmpeg receives prewrapped packets, so the test
does not charge FFmpeg for the core's former compressed gather/header writes.
Eight alternating/reverse-order rounds process at least 20,000 frames each.
No filesystem I/O, corpus allocation or frame logging occurs inside the timed
loop. FFmpeg's internal decoder allocations remain part of its decode cost.
Each stream repetition resets/flushes its codec. Output retains every round.

Build with the same optimization/FP flags for both caller translation units.
Keep the existing FFmpeg library/toolchain fixed. Record CPU, toolchain, library
revision and compiler flags; run idle after all builds finish. Compare per-file
medians and individual alternating rounds, not a pooled corpus mean. A lead
smaller than the repeat spread is inconclusive. These tests do not establish
WMA performance or portability to an unmeasured architecture.

The native rewrite uses immutable Huffman prefix/gain tables and bounded word
reads; SSE2 matrix/transform strides retain scalar alternatives. Small transforms
scatter pre-rotation directly into FFT order. Paired post-rotation writes each
sample once, without a temporary interleave array or destination reads. Validate
scalar/SIMD transforms, malformed input, checkpoint replay and guest ring output
before timing. Keep pacing, mixdown and frontend sample delivery unchanged.

On MSVC, from a developer prompt, set `XMA_BENCH_INPUT`, optionally
`XMA_BENCH_CHANNELS=1` and `XMA_BENCH_BUILD` to a completed CMake build directory;
run `tools\tests\run_xma_native_benchmark.cmd`. With a completed static Make
build, use the equivalent `.sh` script; `CORE_ARCHIVE` may select another build's
archive. `XMA_BENCH_FLAGS` applies explicit compiler flags to the comparison TU
and native decoder sources together. Capture stdout and the toolchain manifest.

Measured results and raw rounds: [native results](xma_native_benchmark_results.txt).
