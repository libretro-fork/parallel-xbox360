/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_BIT_MAP_H_
#define XENIA_BASE_BIT_MAP_H_

#include <cstddef>
#include <cstdint>
#include "libretro/core_bitmap.h"

namespace xe {

// Bit Map: Efficient lookup of free/used entries.
class BitMap {
 public:
  BitMap();
  ~BitMap();
  BitMap(const BitMap&) = delete;
  BitMap& operator=(const BitMap&) = delete;

  // (threadsafe) Acquires an entry and returns its index. Returns -1 if there
  // are no more free entries.
  size_t Acquire();
  size_t AcquireFromBack();
  // (threadsafe) Releases an entry by an index.
  void Release(size_t index);

  // Resize the bitmap. Size is the number of entries, must be a multiple of 64.
  void Resize(size_t new_size_bits);

  // Sets all entries to free.
  void Reset();
  // State owner only: allocation/release/resize users must be quiescent.
  bool CaptureState(void* out, size_t bytes) const;
  bool RestoreState(const void* input, size_t bytes);

 private:
  xe_bitmap data_ = {};
};

}  // namespace xe

#endif  // XENIA_BASE_BIT_MAP_H_
