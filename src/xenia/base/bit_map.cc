/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */
#include "xenia/base/bit_map.h"
#include <new>
#include "xenia/base/assert.h"
namespace xe {
BitMap::BitMap() = default;
BitMap::~BitMap() { xe_bitmap_close(&data_); }
void BitMap::Resize(size_t bits) {
  if (!xe_bitmap_resize(&data_, bits)) throw std::bad_alloc();
}
size_t BitMap::Acquire() { return xe_bitmap_acquire(&data_, 0); }
size_t BitMap::AcquireFromBack() { return xe_bitmap_acquire(&data_, 1); }
void BitMap::Release(size_t index) {
  bool released = xe_bitmap_release(&data_, index) != 0;
  assert_true(released);
}
void BitMap::Reset() { xe_bitmap_reset(&data_); }
}  // namespace xe
