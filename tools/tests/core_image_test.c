/* Guest image codec linkage and exact RGBA round trip; MSVC C89. */
#include <stdio.h>
#include <string.h>
#include "third_party/stb/stb_image.h"
#include "third_party/stb/stb_image_write.h"

struct output {
  unsigned char bytes[4096];
  size_t size;
  int failed;
};

static void write_bytes(void *context, void *data, int size) {
  struct output *out = (struct output *)context;
  if (size < 0 || (size_t)size > sizeof(out->bytes) - out->size) {
    out->failed = 1;
    return;
  }
  memcpy(out->bytes + out->size, data, (size_t)size);
  out->size += (size_t)size;
}

int main(void) {
  static const unsigned char pixels[] = {
    0, 1, 2, 255, 250, 128, 16, 32, 3, 4, 5, 6, 255, 254, 253, 252
  };
  struct output out;
  unsigned char *decoded;
  int width, height, channels, match;
  memset(&out, 0, sizeof(out));
  if (!stbi_write_png_to_func(write_bytes, &out, 2, 2, 4, pixels, 8) ||
      out.failed) return 1;
  if (!stbi_info_from_memory(out.bytes, (int)out.size, &width, &height,
                            &channels) || width != 2 || height != 2 ||
      channels != 4) return 2;
  decoded = stbi_load_from_memory(out.bytes, (int)out.size, &width, &height,
                                  &channels, 4);
  if (!decoded) return 3;
  match = memcmp(decoded, pixels, sizeof(pixels)) == 0;
  stbi_image_free(decoded);
  if (!match) return 4;
  if (stbi_info_from_memory(out.bytes, 3, &width, &height, &channels)) return 5;
  puts("PASS: shared image codec metadata, exact RGBA round trip and truncation");
  return 0;
}
