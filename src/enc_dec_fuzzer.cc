#include <woff2/decode.h>
#include <woff2/encode.h>

#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t data_size) {
  size_t encoded_size = woff2::MaxWOFF2CompressedSize(data, data_size);
  std::string encoded(encoded_size, 0);
  uint8_t* encoded_data = reinterpret_cast<uint8_t*>(&encoded[0]);

  woff2::WOFF2Params params;
  if (!woff2::ConvertTTFToWOFF2(data, data_size, encoded_data, &encoded_size,
                                params)) {
    // Do not record this in the corpus
    return -1;
  }
  encoded.resize(encoded_size);

  // Decode using newer entry pattern.
  // Same pattern as woff2_decompress.
  std::string decoded_output(
      std::min(woff2::ComputeWOFF2FinalSize(encoded_data, encoded.size()),
               woff2::kDefaultMaxSize),
      0);
  woff2::WOFF2StringOut out(&decoded_output);
  woff2::ConvertWOFF2ToTTF(encoded_data, encoded.size(), &out);

  // Convert back to encoded version.
  size_t re_encoded_size = encoded_size;
  std::string re_encoded(re_encoded_size, 0);
  uint8_t* re_encoded_data = reinterpret_cast<uint8_t*>(&re_encoded[0]);
  if (!woff2::ConvertTTFToWOFF2(
          reinterpret_cast<const uint8_t*>(decoded_output.data()),
          decoded_output.size(), re_encoded_data, &re_encoded_size, params)) {
    fprintf(stderr, "Compression failed.\n");
    return -1;
  }
  re_encoded.resize(re_encoded_size);

  // Compressed data == compressed/decompressed/compressed data.
  // Note that compressed/decompressed may not be the same as the original data
  // provided by libfuzzer because our decompression may output a slightly
  // different but functionally equivalent TTF file.
  if (encoded_size != re_encoded_size) {
    __builtin_trap();
  }
  if (memcmp(encoded.data(), re_encoded.data(), encoded_size) != 0) {
    __builtin_trap();
  }

  return 0;
}
