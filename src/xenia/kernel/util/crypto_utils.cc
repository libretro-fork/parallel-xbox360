/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/crypto_utils.h"

#include "libretro/core_crypto.h"

namespace xe {
namespace kernel {
namespace util {

uint8_t xekey_0x19[] = {0xE1, 0xBC, 0x15, 0x9C, 0x73, 0xB1, 0xEA, 0xE9,
                        0xAB, 0x31, 0x70, 0xF3, 0xAD, 0x47, 0xEB, 0xF3};

uint8_t xekey_0x19_devkit[] = {0xDA, 0xB6, 0x9A, 0xD9, 0x8E, 0x28, 0x76, 0x4F,
                               0x97, 0x7E, 0xE2, 0x48, 0x7E, 0x4F, 0x3F, 0x68};

const uint8_t* GetXeKey(uint32_t idx, bool devkit) {
  if (idx != 0x19) {
    return nullptr;
  }

  return devkit ? xekey_0x19_devkit : xekey_0x19;
}

void HmacSha(const uint8_t* key, uint32_t key_size_in, const uint8_t* inp_1,
             uint32_t inp_1_size, const uint8_t* inp_2, uint32_t inp_2_size,
             const uint8_t* inp_3, uint32_t inp_3_size, uint8_t* out,
             uint32_t out_size) {
  xe_hmac_sha1_3(key, key_size_in, inp_1, inp_1_size, inp_2, inp_2_size,
                 inp_3, inp_3_size, out, out_size);
}

void RC4(const uint8_t* key, uint32_t key_size_in, const uint8_t* data,
         uint32_t data_size, uint8_t* out, uint32_t out_size) {
  xe_rc4_16(key, key_size_in, data, data_size, out, out_size);
}

}  // namespace util
}  // namespace kernel
}  // namespace xe
