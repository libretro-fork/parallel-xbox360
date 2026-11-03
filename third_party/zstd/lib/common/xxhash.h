/* Zstandard checksum adapter to the owned libretro-common XXH64. */
#ifndef XE_ZSTD_XXHASH_H
#define XE_ZSTD_XXHASH_H
#include <encodings/xxh64.h>
typedef xxh64_state_t XXH64_state_t;
#define XXH64(data, size, seed) xxh64((data), (size), (seed))
#define XXH64_reset(state, seed) xxh64_reset((state), (seed))
#define XXH64_update(state, data, size) xxh64_update((state), (data), (size))
#define XXH64_digest(state) xxh64_digest(state)
#endif
