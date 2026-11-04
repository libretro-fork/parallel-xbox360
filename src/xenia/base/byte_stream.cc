/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/byte_stream.h"

#include <cstring>

#include "xenia/base/assert.h"

namespace xe {

ByteStream::ByteStream(uint8_t* data, size_t data_length, size_t offset,
                       bool measuring)
    : cursor_{data, data_length, 0, measuring ? 1 : 0} {
  set_offset(offset);
}

ByteStream::~ByteStream() = default;

void ByteStream::Advance(size_t num_bytes) {
  if (!xe_state_advance(&cursor_, num_bytes)) throw ByteStreamError{};
}

void ByteStream::Read(uint8_t* buf, size_t len) {
  if (!xe_state_read(&cursor_, buf, len)) throw ByteStreamError{};
}

void ByteStream::Write(const uint8_t* buf, size_t len) {
  if (!xe_state_write(&cursor_, buf, len)) throw ByteStreamError{};
}

template <>
bool ByteStream::Read() {
  static_assert(sizeof(bool) == sizeof(uint8_t));
  const uint8_t value = Read<uint8_t>();
  if (value > 1) throw ByteStreamError{};
  return value != 0;
}

template <>
std::string ByteStream::Read() {
  return std::string(ReadStringView());
}

template <>
std::u16string ByteStream::Read() {
  std::u16string str;
  size_t len = Read<uint32_t>();
  if (len > remaining() / sizeof(char16_t)) throw ByteStreamError{};
  str.resize(len);
  Read(str.data(), len * sizeof(char16_t));
  return str;
}

}  // namespace xe
