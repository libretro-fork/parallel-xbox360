# Owned ZArchive sources

These files provide the core's random-access ZArchive support. The original
revision is recorded in `../SOURCE_HISTORY.md`; original license notices remain.
Standalone tools, external dependency discovery and packaging have been removed.

Archive blocks retain the standard Zstandard format, implemented here by the
selected owned libretro-common rzstd sources. Blocks are 64 KiB, with the existing
raw-block fallback when compression is not useful. Decoder workspace is retained
per reader after its first compressed block and output goes into the cache block.
The archive access lock serializes use of that workspace.

The broader migration from C++ file streams, filesystem paths and mutexes to
libretro-common is still in progress.
