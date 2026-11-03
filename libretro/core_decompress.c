/* Complete-stream decompression adapters; MSVC C89. */
#include "core_decompress.h"
#include <stdlib.h>
#include <encodings/deflate.h>

int xe_decompress_exact(void *output, size_t output_size,
    const void *input, size_t input_size, int window_bits) {
  void *stream;
  size_t read, wrote;
  int status;
  uint8_t empty;
  if ((!output && output_size) || !input || !input_size) return 0;
  stream = rinflate_new(window_bits);
  if (!stream) return 0;
  rinflate_set_in(stream, (const uint8_t *)input, input_size);
  rinflate_set_out(stream, output_size ? (uint8_t *)output : &empty, output_size);
  status = rinflate_process(stream, &read, &wrote);
  rinflate_free(stream);
  return status == RDEFLATE_PROCESS_END && wrote == output_size;
}

int xe_decompress_alloc(uint8_t **output, size_t *output_size,
    const void *input, size_t input_size, int window_bits,
    size_t initial_capacity, size_t limit) {
  void *stream;
  uint8_t *bytes, *grown;
  size_t capacity, used, read, wrote, room, next;
  int status, ok = 0;
  if (!output || !output_size) return 0;
  *output = NULL; *output_size = 0;
  if (!input || !input_size || !limit) return 0;
  capacity = initial_capacity ? initial_capacity : 4096;
  if (capacity > limit) capacity = limit;
  bytes = (uint8_t *)malloc(capacity);
  if (!bytes) return 0;
  stream = rinflate_new(window_bits);
  if (!stream) { free(bytes); return 0; }
  used = 0;
  rinflate_set_in(stream, (const uint8_t *)input, input_size);
  rinflate_set_out(stream, bytes, capacity);
  for (;;) {
    room = capacity - used;
    status = rinflate_process(stream, &read, &wrote);
    used += wrote;
    if (status == RDEFLATE_PROCESS_END) { ok = 1; break; }
    /* All input is already available. Suspension with free output room is
     * truncated input, rather than a reason to grow or retry indefinitely. */
    if (status != RDEFLATE_PROCESS_NEXT || wrote < room || capacity == limit)
      break;
    next = capacity > limit / 2 ? limit : capacity * 2;
    grown = (uint8_t *)realloc(bytes, next);
    if (!grown) break;
    bytes = grown; capacity = next;
    /* set_out keeps the decoder's history while rebinding after realloc.
     * extend_out alone would leave a stale base pointer if realloc moved it. */
    rinflate_set_out(stream, bytes + used, capacity - used);
  }
  rinflate_free(stream);
  if (!ok) { free(bytes); return 0; }
  *output = bytes; *output_size = used;
  return 1;
}
