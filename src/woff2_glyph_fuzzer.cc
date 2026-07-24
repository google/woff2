#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "glyph.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 2 || size > 10000) {
    return 0;
  }
  woff2::Glyph glyph;
  if (!woff2::ReadGlyph(data, size, &glyph)) {
    return 0;
  }

  size_t dst_size = size * 2 + 1024; // Provide enough space
  std::vector<uint8_t> dst(dst_size);
  woff2::StoreGlyph(glyph, dst.data(), &dst_size);

  return 0;
}
