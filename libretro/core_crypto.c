#include "core_crypto.h"
#include <string.h>
#include <crypto/crypto.h>
#include <crypto/kdf.h>

int xe_hmac_sha1_3(const uint8_t *key, size_t key_size,
    const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size,
    const uint8_t *c, size_t c_size, uint8_t *out, size_t out_size) {
  struct hmac_sha1_ctx state;
  uint8_t digest[20];
  if (!out_size) return 1;
  if (!out || (!key && key_size) || (!a && a_size) ||
      (!b && b_size) || (!c && c_size)) return 0;
  hmac_sha1_init(&state, key_size ? key : digest, key_size);
  if (a_size) hmac_sha1_update(&state, a, a_size);
  if (b_size) hmac_sha1_update(&state, b, b_size);
  if (c_size) hmac_sha1_update(&state, c, c_size);
  hmac_sha1_final(&state, out_size >= 20 ? out : digest);
  if (out_size < 20) {
    memcpy(out, digest, out_size);
    crypto_memzero(digest, sizeof(digest));
  }
  return 1;
}

int xe_rc4_16(const uint8_t *key, size_t key_size, const uint8_t *input,
    size_t size, uint8_t *out, size_t out_size) {
  uint8_t state[256], temp;
  unsigned i, j, x;
  size_t at;
  uintptr_t source, target;
  if (!size) return 1;
  if (!key || key_size < 16 || !input || !out || out_size < size) return 0;
  source = (uintptr_t)input; target = (uintptr_t)out;
  if (source != target && (source < target ? target - source : source - target) < size)
    return 0;
  for (x = 0; x < 256; ++x) state[x] = (uint8_t)x;
  j = 0;
  for (x = 0; x < 256; ++x) {
    j = (j + state[x] + key[x & 15]) & 255;
    temp = state[x]; state[x] = state[j]; state[j] = temp;
  }
  i = j = 0;
  for (at = 0; at < size; ++at) {
    i = (i + 1) & 255; j = (j + state[i]) & 255;
    temp = state[i]; state[i] = state[j]; state[j] = temp;
    out[at] = input[at] ^ state[(state[i] + state[j]) & 255];
  }
  crypto_memzero(state, sizeof(state));
  return 1;
}
