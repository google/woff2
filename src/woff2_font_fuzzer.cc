#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "font.h"
#include "normalize.h"
#include "transform.h"
#include "woff2_common.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 10 || size > 1000000) {
    return 0;
  }
  woff2::FontCollection font_collection;
  if (!woff2::ReadFontCollection(data, size, &font_collection)) {
    return 0;
  }

  woff2::NormalizeFontCollection(&font_collection);
  
  for (auto& font : font_collection.fonts) {
    woff2::TransformGlyfAndLocaTables(&font);
    woff2::TransformHmtxTable(&font);
  }

  size_t out_size = woff2::FontCollectionFileSize(font_collection);
  // Robust size calculation for TTC header
  if (font_collection.flavor == woff2::kTtcFontFlavor) {
    size_t header_size = 12 + 4 * font_collection.fonts.size();
    if (font_collection.header_version == 0x00020000) {
      header_size += 12;
    }
    out_size = std::max(out_size, header_size);
  }

  if (out_size == 0 || out_size > 30 * 1024 * 1024) {
    return 0;
  }
  
  // Add some padding to be safe against library bugs
  std::vector<uint8_t> out(out_size + 1024);
  woff2::WriteFontCollection(font_collection, out.data(), out.size());

  return 0;
}
