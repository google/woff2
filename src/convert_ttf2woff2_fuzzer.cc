#include <stddef.h>
#include <stdint.h>
#include <vector>

#include <woff2/encode.h>

// Entry point for LibFuzzer.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  size_t result_length = woff2::MaxWOFF2CompressedSize(data, size);
  if (result_length == 0) {
    return 0;
  }
  std::vector<uint8_t> result(result_length);
  woff2::WOFF2Params params;
  params.brotli_quality = 1;
  woff2::ConvertTTFToWOFF2(data, size, result.data(), &result_length, params);
  return 0;
}
