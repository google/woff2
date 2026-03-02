/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* Helper functions for storing integer values into byte streams.
   No bounds checking is performed, that is the responsibility of the caller. */

#ifndef WOFF2_STORE_BYTES_H_
#define WOFF2_STORE_BYTES_H_

#include <inttypes.h>
#include <stddef.h>
#include <string.h>

#include <span>

#include "./port.h"

namespace woff2 {

inline size_t StoreU32(std::span<uint8_t> dst, size_t offset, uint32_t x) {
  dst[offset] = x >> 24;
  dst[offset + 1] = x >> 16;
  dst[offset + 2] = x >> 8;
  dst[offset + 3] = x;
  return offset + 4;
}

inline size_t Store16(std::span<uint8_t> dst, size_t offset, int x) {
  dst[offset] = x >> 8;
  dst[offset + 1] = x;
  return offset + 2;
}

inline void StoreU32(uint32_t val, size_t* offset, std::span<uint8_t> dst) {
  dst[(*offset)++] = val >> 24;
  dst[(*offset)++] = val >> 16;
  dst[(*offset)++] = val >> 8;
  dst[(*offset)++] = val;
}

inline void Store16(int val, size_t* offset, std::span<uint8_t> dst) {
  dst[(*offset)++] = val >> 8;
  dst[(*offset)++] = val;
}

inline void StoreBytes(std::span<const uint8_t> data,
                       size_t* offset, std::span<uint8_t> dst) {
  std::copy(data.begin(), data.end(), &dst[*offset]);
  *offset += data.size_bytes();
}

// Same as above, but with less safety.

inline size_t StoreU32(uint8_t* dst, size_t offset, uint32_t x) {
  return StoreU32(std::span(dst, sizeof(uint32_t)), offset, x);
}

inline size_t Store16(uint8_t* dst, size_t offset, int x) {
  return Store16(std::span(dst, sizeof(uint16_t)), offset, x);
}

inline void StoreU32(uint32_t val, size_t* offset, uint8_t* dst) {
  StoreU32(val, offset, std::span(dst, sizeof(uint32_t)));
}

inline void Store16(int val, size_t* offset, uint8_t* dst) {
  return Store16(val, offset, std::span(dst, sizeof(uint16_t)));
}

inline void StoreBytes(const uint8_t* data, size_t len,
                       size_t* offset, uint8_t* dst) {
  StoreBytes(std::span(data, len), offset, std::span(dst, len));
}

} // namespace woff2

#endif  // WOFF2_STORE_BYTES_H_
