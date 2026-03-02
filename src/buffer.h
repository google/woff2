/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* The parts of ots.h & opentype-sanitiser.h that we need, taken from the
   https://code.google.com/p/ots/ project. */

#ifndef WOFF2_BUFFER_H_
#define WOFF2_BUFFER_H_

#if defined(_WIN32)
#include <stdlib.h>
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#define ntohl(x) _byteswap_ulong (x)
#define ntohs(x) _byteswap_ushort (x)
#define htonl(x) _byteswap_ulong (x)
#define htons(x) _byteswap_ushort (x)
#else
#include <arpa/inet.h>
#include <stdint.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>

namespace woff2 {

#if defined(_MSC_VER) || !defined(FONT_COMPRESSION_DEBUG)
#define FONT_COMPRESSION_FAILURE() false
#else
#define FONT_COMPRESSION_FAILURE() \
  woff2::Failure(__FILE__, __LINE__, __PRETTY_FUNCTION__)
inline bool Failure(const char *f, int l, const char *fn) {
  fprintf(stderr, "ERROR at %s:%d (%s)\n", f, l, fn);
  fflush(stderr);
  return false;
}
#endif

// -----------------------------------------------------------------------------
// Buffer helper class
//
// This class perform some trival buffer operations while checking for
// out-of-bounds errors. As a family they return false if anything is amiss,
// updating the current offset otherwise.
// -----------------------------------------------------------------------------
class Buffer {
 public:
  Buffer(const uint8_t *data, size_t len)
      : buffer_(data, len),
        offset_(0) { }
  Buffer(std::span<const uint8_t> data) : buffer_(data), offset_(0) { }

  bool Skip(size_t n_bytes) {
    return Read(std::span<uint8_t>(), n_bytes);
  }

  bool Read(std::span<uint8_t> data, size_t n_bytes) {
    if (n_bytes > 1024 * 1024 * 1024) {
      return FONT_COMPRESSION_FAILURE();
    }
    if (n_bytes > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    if (data.size() != 0) {
      std::span<const uint8_t> buffer_view =
          remaining_buffer().subspan(0, n_bytes);
      std::copy(buffer_view.begin(), buffer_view.end(), data.begin());
    }
    offset_ += n_bytes;
    return true;
  }

  inline bool ReadU8(uint8_t *value) {
    if (1 > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    *value = buffer_[offset_];
    ++offset_;
    return true;
  }

  bool ReadU16(uint16_t *value) {
    if (2 > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    *value = static_cast<uint32_t>(buffer_[offset_]) << 8 |
        static_cast<uint32_t>(buffer_[offset_ + 1]);
    offset_ += 2;
    return true;
  }

  bool ReadS16(int16_t *value) {
    return ReadU16(reinterpret_cast<uint16_t*>(value));
  }

  bool ReadU24(uint32_t *value) {
    if (3 > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    *value = static_cast<uint32_t>(buffer_[offset_]) << 16 |
        static_cast<uint32_t>(buffer_[offset_ + 1]) << 8 |
        static_cast<uint32_t>(buffer_[offset_ + 2]);
    offset_ += 3;
    return true;
  }

  bool ReadU32(uint32_t *value) {
    if (4 > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    *value = static_cast<uint32_t>(buffer_[offset_]) << 24 |
        static_cast<uint32_t>(buffer_[offset_ + 1]) << 16 |
        static_cast<uint32_t>(buffer_[offset_ + 2]) << 8 |
        static_cast<uint32_t>(buffer_[offset_ + 3]);
    offset_ += 4;
    return true;
  }

  bool ReadS32(int32_t *value) {
    return ReadU32(reinterpret_cast<uint32_t*>(value));
  }

  bool ReadTag(uint32_t *value) {
    if (4 > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    *value = static_cast<uint32_t>(buffer_[offset_]) |
        static_cast<uint32_t>(buffer_[offset_ + 1]) << 8 |
        static_cast<uint32_t>(buffer_[offset_ + 2]) << 16 |
        static_cast<uint32_t>(buffer_[offset_ + 3]) << 24;
    offset_ += 4;
    return true;
  }

  bool ReadR64(uint64_t *value) {
    if (8 > remaining_length()) {
      return FONT_COMPRESSION_FAILURE();
    }
    *value = static_cast<uint64_t>(buffer_[offset_]) << 56 |
        static_cast<uint64_t>(buffer_[offset_ + 1]) << 48 |
        static_cast<uint64_t>(buffer_[offset_ + 2]) << 40 |
        static_cast<uint64_t>(buffer_[offset_ + 3]) << 32 |
        static_cast<uint64_t>(buffer_[offset_ + 4]) << 24 |
        static_cast<uint64_t>(buffer_[offset_ + 5]) << 16 |
        static_cast<uint64_t>(buffer_[offset_ + 6]) << 8 |
        static_cast<uint64_t>(buffer_[offset_ + 7]);
    offset_ += 8;
    return true;
  }

  inline std::span<const uint8_t> remaining_buffer() const {
    return buffer_.subspan(offset_);
  };
  inline size_t remaining_length() const { return remaining_buffer().size(); }

  inline size_t offset() { return offset_; }

  void set_offset(size_t newoffset) {
    offset_ = newoffset;
  }

 private:
  // A view of the unowned buffer.
  const std::span<const uint8_t> buffer_;
  size_t offset_;
};

} // namespace woff2

#endif  // WOFF2_BUFFER_H_
