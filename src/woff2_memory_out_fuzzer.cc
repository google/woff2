#include <stddef.h>
#include <stdint.h>
#include <vector>

#include <woff2/decode.h>
#include <woff2/output.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  size_t final_size = woff2::ComputeWOFF2FinalSize(data, size);
  if (final_size == 0 || final_size > 30 * 1024 * 1024) {
    return 0;
  }

  std::vector<uint8_t> result(final_size);
  woff2::WOFF2MemoryOut out(result.data(), result.size());
  woff2::ConvertWOFF2ToTTF(data, size, &out);
  return 0;
}
