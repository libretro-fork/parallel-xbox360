#include "core_guest_hash.h"
#include <string.h>
#include <lrc_hash.h>

static uint32_t guest_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
      ((uint32_t)p[2] << 8) | p[3];
}
static void guest_put32(uint8_t *p, uint32_t value) {
  p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
  p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}
static void sha256_import(struct sha256_state *p, const uint8_t *guest) {
  unsigned i;
  uint32_t count = guest_be32(guest);
  p->len = count; p->inlen = count & 63; p->is224 = 0;
  for (i = 0; i < 8; ++i) p->h[i] = guest_be32(guest + 4 + i * 4);
  /* The remaining input bytes and message schedule are scratch. */
  if (p->inlen) memcpy(p->in.u8, guest + 36, p->inlen);
}
int xe_guest_sha256_init(uint8_t *guest) {
  static const uint8_t words[32] = {
    0x6a,0x09,0xe6,0x67,0xbb,0x67,0xae,0x85,
    0x3c,0x6e,0xf3,0x72,0xa5,0x4f,0xf5,0x3a,
    0x51,0x0e,0x52,0x7f,0x9b,0x05,0x68,0x8c,
    0x1f,0x83,0xd9,0xab,0x5b,0xe0,0xcd,0x19
  };
  if (!guest) return 0;
  memset(guest, 0, 4); memcpy(guest + 4, words, sizeof(words));
  memset(guest + 36, 0, 64);
  return 1;
}
int xe_guest_sha256_update(uint8_t *guest, const uint8_t *input, size_t size) {
  struct sha256_state p;
  uint32_t count;
  unsigned within, i;
  size_t changed;
  if (!guest || (!input && size)) return 0;
  if (!size) return 1;
  count = guest_be32(guest); within = count & 63;
  if (size < 64 - within) {
    memmove(guest + 36 + within, input, size);
    guest_put32(guest, count + (uint32_t)size);
    return 1;
  }
  sha256_import(&p, guest);
  sha256_stream_update(&p, input, size);
  for (i = 0; i < 8; ++i) guest_put32(guest + 4 + i * 4, p.h[i]);
  /* A completed partial block remains visible; direct full blocks never
   * replace the guest buffer. Only their final partial tail changes it. */
  changed = within ? 64 : p.inlen;
  if (changed) memcpy(guest + 36, p.in.u8, changed);
  guest_put32(guest, (uint32_t)p.len);
  return 1;
}
int xe_guest_sha256_final(uint8_t *guest, uint8_t *out, size_t out_size) {
  struct sha256_state p;
  size_t count = out_size < 32 ? out_size : 32;
  if (!guest || (!out && count)) return 0;
  sha256_import(&p, guest);
  /* Final compression consumes the tail before this buffer becomes digest
   * storage. Retain it privately until both guest destinations are written. */
  sha256_stream_final(&p, p.in.u8);
  if (count) memcpy(out, p.in.u8, count);
  memcpy(guest + 36, p.in.u8, 32);
  return 1;
}
