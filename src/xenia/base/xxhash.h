/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_XXHASH_H_
#define XENIA_BASE_XXHASH_H_

#include <encodings/xxh3.h>

/* Keep persistent cache keys identical. Dispatch is published via retro_atomic. */
typedef xxh3_state_t XXH3_state_t;
#define XXH3_64bits(data, size) xxh3_64((data), (size), 0)
#define XXH3_64bits_reset(state) xxh3_reset((state), 0)
#define XXH3_64bits_update(state, data, size) xxh3_update((state), (data), (size))
#define XXH3_64bits_digest(state) xxh3_digest(state)

#endif  // XENIA_BASE_XXHASH_H_
