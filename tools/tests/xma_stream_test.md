# Offline XMA stream regression

This runs the real XmaContextNew decoder and guest PCM ring against a non-silent
XMA2 stream. It needs no game, RetroArch, renderer, audio device or wall-clock
pacing. Build the native core libraries first, then run the appropriate
run_xma_stream_tests script from a configured MSVC prompt or Make environment.

Fixture: https://samples.ffmpeg.org/game-formats/xma/Title_Call.xma
The fixture is external test data; it is not distributed with this repository.
Extract its RIFF `data` chunk (102400 bytes) to a local file and set
XMA_INPUT_RAW to its absolute path. No download occurs during the test.

The test checks each decoder write-cursor update against actual decoded blocks.
A 256-frame stereo consumer then advances through the ring. Twelve combinations
of ring capacity and decode quantum, and twelve alternating-buffer streaming
configurations, must produce identical non-silent PCM with identical lengths. Each run also temporarily removes compressed input while PCM remains queued; that pass must preserve the write cursor.
Set XMA_OUTPUT_REFERENCE to optionally save the baseline big-endian int16 PCM.

Before the fix, starvation moved the write cursor without writing any PCM;
eleven of the twelve non-streaming configurations failed. With the fix,
all 24 configurations produce 770048 identical PCM bytes for this fixture.
A negative-control build using the pre-fix decoder discards unread PCM at the third consumer quantum during these repeated-starvation checks. The fixed decoder preserves the complete sequence. This catches decoder data loss. It does not establish that Tekken's complete
gameplay scheduling and audio regression has been fixed.
