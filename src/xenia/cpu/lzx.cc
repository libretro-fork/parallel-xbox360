/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/lzx.h"
#include "libretro/core_xex_delta.h"

#include <climits>
#include <cstdlib>
#include <cstring>

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/kernel/util/xex2_info.h"

#include "third_party/mspack/lzx.h"
#include "third_party/mspack/mspack.h"

typedef struct mspack_memory_file_t {
  void* buffer;
  off_t buffer_size;
  off_t offset;
} mspack_memory_file;

mspack_memory_file* mspack_memory_open(mspack_system* sys, void* buffer,
                                       const size_t buffer_size) {
  assert_true(buffer_size < INT_MAX);
  if (buffer_size >= INT_MAX) {
    return NULL;
  }
  auto memfile =
      (mspack_memory_file*)std::malloc(sizeof(mspack_memory_file));
  if (!memfile) {
    return NULL;
  }
  memfile->buffer = buffer;
  memfile->buffer_size = (off_t)buffer_size;
  memfile->offset = 0;
  return memfile;
}

void mspack_memory_close(mspack_memory_file* file) {
  auto memfile = (mspack_memory_file*)file;
  std::free(memfile);
}

int mspack_memory_read(mspack_file* file, void* buffer, int chars) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  off_t remaining, total;
  if (!memfile || chars < 0 || memfile->offset < 0 || memfile->offset > memfile->buffer_size) return -1;
  remaining = memfile->buffer_size - memfile->offset;
  if (!chars || !remaining) return 0;
  if (!buffer || !memfile->buffer) return -1;
  total = chars < remaining ? chars : remaining;
  std::memcpy(buffer, (uint8_t*)memfile->buffer + memfile->offset, (size_t)total);
  memfile->offset += total; return (int)total;
}

int mspack_memory_write(mspack_file* file, void* buffer, int chars) {
  mspack_memory_file* memfile = (mspack_memory_file*)file;
  off_t remaining, total;
  if (!memfile || chars < 0 || memfile->offset < 0 || memfile->offset > memfile->buffer_size) return -1;
  remaining = memfile->buffer_size - memfile->offset;
  if (!chars || !remaining) return 0;
  if (!buffer || !memfile->buffer) return -1;
  total = chars < remaining ? chars : remaining;
  std::memcpy((uint8_t*)memfile->buffer + memfile->offset, buffer, (size_t)total);
  memfile->offset += total; return (int)total;
}
void* mspack_memory_alloc(mspack_system* sys, size_t chars) {
  return std::calloc(chars, 1);
}

void mspack_memory_free(void* ptr) { std::free(ptr); }

void mspack_memory_copy(void* src, void* dest, size_t chars) {
  std::memcpy(dest, src, chars);
}

static void mspack_memory_sys_init(mspack_system* sys) {
  std::memset(sys, 0, sizeof(*sys));
  sys->read = mspack_memory_read; sys->write = mspack_memory_write;
  sys->alloc = mspack_memory_alloc; sys->free = mspack_memory_free;
  sys->copy = mspack_memory_copy;
}
mspack_system* mspack_memory_sys_create() {
  mspack_system* sys = (mspack_system*)std::malloc(sizeof(mspack_system));
  if (sys) mspack_memory_sys_init(sys);
  return sys;
}
void mspack_memory_sys_destroy(struct mspack_system* sys) { free(sys); }

int lzx_decompress(const void* lzx_data, size_t lzx_len, void* dest,
                   size_t dest_len, uint32_t window_size, void* window_data,
                   size_t window_data_len) {
  int result_code = 1;
  uint32_t window_bits;
  mspack_system system;
  mspack_memory_file input, output;
  lzxd_stream* lzxd;
  size_t padding_len;
  if (!dest_len) return 0;
  if (!lzx_data || !lzx_len || lzx_len >= INT_MAX || !dest || dest_len >= INT_MAX || !window_size ||
      (window_size & (window_size - 1)) || window_data_len > window_size ||
      (!window_data && window_data_len)) return result_code;

  if (!xe::bit_scan_forward(window_size, &window_bits)) {
    return result_code;
  }

  mspack_memory_sys_init(&system);
  input.buffer = const_cast<void*>(lzx_data); input.buffer_size = (off_t)lzx_len; input.offset = 0;
  output.buffer = dest; output.buffer_size = (off_t)dest_len; output.offset = 0;
  lzxd = lzxd_init(&system, (mspack_file*)&input, (mspack_file*)&output,
      window_bits, 0, 0x8000, (off_t)dest_len, 0);
  if (lzxd) {
    if (window_data) {
      // zero the window and then copy window_data to the end of it
      padding_len = window_size - window_data_len;
      std::memset(&lzxd->window[0], 0, padding_len);
      std::memcpy(&lzxd->window[padding_len], window_data, window_data_len);
      // TODO(gibbed): should this be set regardless if source window data is
      // available or not?
      lzxd->ref_data_size = window_size;
    }

    result_code = lzxd_decompress(lzxd, (off_t)dest_len);

    lzxd_free(lzxd);
    lzxd = NULL;
  }

  return result_code;
}

LzxFrameDecoder::LzxFrameDecoder(uint32_t window_size)
    : system_(mspack_memory_sys_create()),
      input_(mspack_memory_open(system_, NULL, 0)),
      output_(mspack_memory_open(system_, NULL, 0)) {
  if (window_size >= 32768 && window_size <= 2097152 &&
      !(window_size & (window_size - 1)))
    xe::bit_scan_forward(window_size, &window_bits_);
}

LzxFrameDecoder::~LzxFrameDecoder() {
  lzxd_free(stream_);
  mspack_memory_close(output_);
  mspack_memory_close(input_);
  mspack_memory_sys_destroy(system_);
}

bool LzxFrameDecoder::Reset() {
  lzxd_free(stream_);
  stream_ = NULL;
  if (system_ && input_ && output_ && window_bits_) {
    stream_ = lzxd_init(system_, (mspack_file*)input_, (mspack_file*)output_,
                        window_bits_, 0, 0x8000, 0, 0);
  }
  return stream_ != NULL;
}

bool LzxFrameDecoder::Decompress(const void* source, size_t source_size,
                                 void* dest, size_t dest_size) {
  if (!stream_ || !source || !source_size || source_size >= INT_MAX ||
      !dest || !dest_size || dest_size > LZX_FRAME_SIZE) {
    return false;
  }
  input_->buffer = (void*)source;
  input_->buffer_size = (off_t)source_size;
  input_->offset = 0;
  output_->buffer = dest;
  output_->buffer_size = (off_t)dest_size;
  output_->offset = 0;

  // Drop what lzxd buffered past the end of the previous frame, including the
  // padding it reads at the end of input.
  stream_->i_ptr = stream_->i_end = stream_->inbuf;
  stream_->bit_buffer = 0;
  stream_->bits_left = 0;
  stream_->input_end = 0;

  // lzxd only ends a frame short at the length of the stream. Output that ends
  // on a frame boundary makes it count an extra empty frame, which fails a
  // later short frame, so the last byte is asked for on its own.
  lzxd_set_output_length(stream_, stream_->offset + (off_t)dest_size);
  return lzxd_decompress(stream_, (off_t)dest_size - 1) == MSPACK_ERR_OK &&
         lzxd_decompress(stream_, 1) == MSPACK_ERR_OK;
}

int lzxdelta_apply_patch(const void* patch, size_t patch_len,
    uint32_t window_size, void* dest, size_t destination_size, size_t reference_size) {
  return xe_delta_apply(static_cast<const uint8_t*>(patch), patch_len, window_size,
      static_cast<uint8_t*>(dest), destination_size, reference_size,
      [](void*, const uint8_t* data, size_t size, uint8_t* output, size_t length,
          uint32_t window, const uint8_t* reference, size_t reference_length) {
        return lzx_decompress(data, size, output, length, window,
            const_cast<uint8_t*>(reference), reference_length);
      }, nullptr);
}
