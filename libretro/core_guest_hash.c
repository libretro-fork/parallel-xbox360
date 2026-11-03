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

static uint64_t guest_be64(const uint8_t *p) {
  return ((uint64_t)guest_be32(p) << 32) | guest_be32(p + 4);
}
static void guest_put64(uint8_t *p, uint64_t value) {
  guest_put32(p, (uint32_t)(value >> 32)); guest_put32(p + 4, (uint32_t)value);
}
static void md5_import(MD5_CTX *p, const uint8_t *guest) {
  uint64_t count = guest_be64(guest);
  unsigned within = (unsigned)(count & 63);
  p->lo = (uint32_t)count & 0x1fffffffU;
  p->hi = (uint32_t)(count >> 29);
  /* Guest words retain FFmpeg's reversed D,C,B,A ordering. */
  p->a = guest_be32(guest + 20); p->b = guest_be32(guest + 16);
  p->c = guest_be32(guest + 12); p->d = guest_be32(guest + 8);
  if (within) memcpy(p->buffer, guest + 24, within);
}
int xe_guest_md5_init(uint8_t *guest) {
  MD5_CTX p;
  if (!guest) return 0;
  MD5_Init(&p);
  memset(guest, 0, XE_GUEST_MD5_SIZE);
  guest_put32(guest + 8, p.d); guest_put32(guest + 12, p.c);
  guest_put32(guest + 16, p.b); guest_put32(guest + 20, p.a);
  return 1;
}
int xe_guest_md5_update(uint8_t *guest, const uint8_t *input, size_t size) {
  MD5_CTX p;
  uint64_t count;
  unsigned within;
  size_t changed, remaining = size;
  if (!guest || (!input && size)) return 0;
  if (!size) return 1;
  count = guest_be64(guest); within = (unsigned)(count & 63);
  if (size < 64 - within) {
    memmove(guest + 24 + within, input, size);
    guest_put64(guest, count + size);
    return 1;
  }
  md5_import(&p, guest);
  while (remaining) {
    /* End large chunks at a block boundary so later calls do not
     * replace the first completed partial block retained in p.buffer. */
    unsigned long chunk = remaining > 0x10000000U ?
        0x10000000UL - (p.lo & 63) : (unsigned long)remaining;
    MD5_Update(&p, input, chunk);
    input += chunk; remaining -= chunk;
  }
  guest_put32(guest + 8, p.d); guest_put32(guest + 12, p.c);
  guest_put32(guest + 16, p.b); guest_put32(guest + 20, p.a);
  changed = within ? 64 : (size_t)((count + size) & 63);
  if (changed) memcpy(guest + 24, p.buffer, changed);
  guest_put64(guest, count + size);
  return 1;
}
int xe_guest_md5_final(uint8_t *guest, uint8_t *out, size_t out_size) {
  MD5_CTX p;
  uint8_t digest[16];
  size_t size = out_size < 16 ? out_size : 16;
  if (!guest || (!out && size)) return 0;
  md5_import(&p, guest); MD5_Final(digest, &p);
  if (size) memcpy(out, digest, size);
  memcpy(guest + 24, digest, sizeof(digest));
  return 1;
}
static void sha512_import(struct sha512_state *p, const uint8_t *guest) {
  unsigned i;
  p->len = guest_be64(guest); p->buflen = (unsigned)(p->len & 127); p->is384 = 0;
  for (i = 0; i < 8; ++i) p->h[i] = guest_be64(guest + 8 + i * 8);
  if (p->buflen) memcpy(p->buf, guest + 72, p->buflen);
}
int xe_guest_sha512_init(uint8_t *guest) {
  struct sha512_state p;
  unsigned i;
  if (!guest) return 0;
  sha512_stream_init(&p, 0);
  memset(guest, 0, XE_GUEST_SHA512_SIZE);
  for (i = 0; i < 8; ++i) guest_put64(guest + 8 + i * 8, p.h[i]);
  return 1;
}
int xe_guest_sha512_update(uint8_t *guest, const uint8_t *input, size_t size) {
  struct sha512_state p;
  uint64_t count;
  unsigned within, i;
  if (!guest || (!input && size)) return 0;
  if (!size) return 1;
  count = guest_be64(guest); within = (unsigned)(count & 127);
  if (size < 128 - within) {
    memmove(guest + 72 + within, input, size);
    guest_put64(guest, count + size);
    return 1;
  }
  sha512_import(&p, guest);
  /* FFmpeg leaves the first completed block in the guest buffer even
   * when the initial count is aligned. Retain that observable state while
   * compression borrows subsequent complete blocks directly. */
  if (!within) memcpy(p.buf, input, 128);
  sha512_stream_update(&p, input, size);
  for (i = 0; i < 8; ++i) guest_put64(guest + 8 + i * 8, p.h[i]);
  memcpy(guest + 72, p.buf, 128);
  guest_put64(guest, count + size);
  return 1;
}
int xe_guest_sha512_final(uint8_t *guest, uint8_t *out, size_t out_size) {
  struct sha512_state p;
  uint8_t digest[64];
  size_t size = out_size < 64 ? out_size : 64;
  if (!guest || (!out && size)) return 0;
  sha512_import(&p, guest); sha512_stream_final(&p, digest);
  if (size) memcpy(out, digest, size);
  memcpy(guest + 72, digest, sizeof(digest));
  return 1;
}
