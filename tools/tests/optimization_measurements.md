# Hash replacement measurements

3 November 2026; Windows x64, Ryzen 9 9950X3D, MSVC `/O2`, no active core
builds or gameplay. Compare the former inline XXH3 implementation against the
owned libretro-common `xxh3_64` function with seed zero. Inputs stay warm and
cycle through 16 byte offsets. Each process alternates five before/after pairs;
the table takes the median of five process medians. The loop consumes results.
These are function microbenchmarks, not frame-time or gameplay measurements.

| Bytes | Former inline ns/call | libretro-common ns/call | Former/common |
| ---: | ---: | ---: | ---: |
| 16 | 1.173 | 1.624 | 0.722 |
| 32 | 1.249 | 1.708 | 0.731 |
| 64 | 1.901 | 2.456 | 0.774 |
| 128 | 3.214 | 4.182 | 0.769 |
| 240 | 8.886 | 7.969 | 1.115 |
| 256 | 18.412 | 9.108 | 2.022 |
| 1024 | 28.638 | 12.801 | 2.237 |
| 65536 | 1349.219 | 696.387 | 1.937 |
| 1048576 | 21710.938 | 11175.781 | 1.943 |

Bulk inputs improve; tiny fixed keys regress by about 0.5 to 1 ns per call.
This does not establish universal speedups. Tiny-key call overhead remains an
optimization candidate. Legacy golden folds and a separate old/new differential
check verify values across unaligned inputs, seeded and streaming boundaries.

The embedded patch bundle now carries its decoded size in generated metadata.
It allocates that size once and adopts the decoded allocation; it no longer
zero-fills a vector at four times the compressed size and then grows/copies it.
This is a source-proven allocation/copy removal, not a measured startup-time gain.
