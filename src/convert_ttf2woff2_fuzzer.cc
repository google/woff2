#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

#include <woff2/encode.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  if (size < 1) {
    return 0;
  }
  
  woff2::WOFF2Params params;
  uint8_t first_byte = data[0];
  params.allow_transforms = first_byte & 1;
  params.brotli_quality = (first_byte >> 1) % 12; // 0-11
  
  size_t metadata_size = 0;
  if (size > 2) {
    metadata_size = data[1] % (size - 1);
  }
  
  if (metadata_size > 0) {
    params.extended_metadata.assign(reinterpret_cast<const char*>(data + 2), metadata_size);
  }
  
  size_t offset = 2 + metadata_size;
  if (offset > size) {
    return 0;
  }
  
  const uint8_t* font_data = data + offset;
  size_t font_size = size - offset;
  
  if (font_size == 0) {
    return 0;
  }

  size_t result_length = woff2::MaxWOFF2CompressedSize(font_data, font_size, params.extended_metadata);
  if (result_length > 30 * 1024 * 1024) {
    return 0;
  }
  std::vector<uint8_t> result(result_length);
  woff2::ConvertTTFToWOFF2(font_data, font_size, result.data(), &result_length, params);
  return 0;
}
