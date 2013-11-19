// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Library for converting WOFF2 format font files to their TTF versions.

#include "./woff2.h"

#include <stdlib.h>
#include <complex>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "./ots.h"
#include "./decode.h"
#include "./encode.h"
#include "./font.h"
#include "./normalize.h"
#include "./round.h"
#include "./store_bytes.h"
#include "./transform.h"

namespace woff2 {

namespace {

using std::string;
using std::vector;



// simple glyph flags
const int kGlyfOnCurve = 1 << 0;
const int kGlyfXShort = 1 << 1;
const int kGlyfYShort = 1 << 2;
const int kGlyfRepeat = 1 << 3;
const int kGlyfThisXIsSame = 1 << 4;
const int kGlyfThisYIsSame = 1 << 5;

// composite glyph flags
const int FLAG_ARG_1_AND_2_ARE_WORDS = 1 << 0;
const int FLAG_ARGS_ARE_XY_VALUES = 1 << 1;
const int FLAG_ROUND_XY_TO_GRID = 1 << 2;
const int FLAG_WE_HAVE_A_SCALE = 1 << 3;
const int FLAG_RESERVED = 1 << 4;
const int FLAG_MORE_COMPONENTS = 1 << 5;
const int FLAG_WE_HAVE_AN_X_AND_Y_SCALE = 1 << 6;
const int FLAG_WE_HAVE_A_TWO_BY_TWO = 1 << 7;
const int FLAG_WE_HAVE_INSTRUCTIONS = 1 << 8;
const int FLAG_USE_MY_METRICS = 1 << 9;
const int FLAG_OVERLAP_COMPOUND = 1 << 10;
const int FLAG_SCALED_COMPONENT_OFFSET = 1 << 11;
const int FLAG_UNSCALED_COMPONENT_OFFSET = 1 << 12;

const size_t kSfntHeaderSize = 12;
const size_t kSfntEntrySize = 16;
const size_t kCheckSumAdjustmentOffset = 8;

const size_t kEndPtsOfContoursOffset = 10;
const size_t kCompositeGlyphBegin = 10;

// Note that the byte order is big-endian, not the same as ots.cc
#define TAG(a, b, c, d) ((a << 24) | (b << 16) | (c << 8) | d)

const uint32_t kWoff2Signature = 0x774f4632;  // "wOF2"

const unsigned int kWoff2FlagsContinueStream = 1 << 4;
const unsigned int kWoff2FlagsTransform = 1 << 5;

const size_t kWoff2HeaderSize = 44;
const size_t kWoff2EntrySize = 20;

const size_t kLzmaHeaderSize = 13;

// Compression type values common to both short and long formats
const uint32_t kCompressionTypeMask = 0xf;
const uint32_t kCompressionTypeNone = 0;
const uint32_t kCompressionTypeGzip = 1;
const uint32_t kCompressionTypeLzma = 2;
const uint32_t kCompressionTypeBrotli = 3;
const uint32_t kCompressionTypeLzham = 4;

// This is a special value for the short format only, as described in
// "Design for compressed header format" in draft doc.
const uint32_t kShortFlagsContinue = 3;

struct Point {
  int x;
  int y;
  bool on_curve;
};

struct Table {
  uint32_t tag;
  uint32_t flags;
  uint32_t src_offset;
  uint32_t src_length;

  uint32_t transform_length;

  uint32_t dst_offset;
  uint32_t dst_length;
  const uint8_t* dst_data;
};

// Based on section 6.1.1 of MicroType Express draft spec
bool Read255UShort(ots::Buffer* buf, unsigned int* value) {
  static const int kWordCode = 253;
  static const int kOneMoreByteCode2 = 254;
  static const int kOneMoreByteCode1 = 255;
  static const int kLowestUCode = 253;
  uint8_t code = 0;
  if (!buf->ReadU8(&code)) {
    return OTS_FAILURE();
  }
  if (code == kWordCode) {
    uint16_t result = 0;
    if (!buf->ReadU16(&result)) {
      return OTS_FAILURE();
    }
    *value = result;
    return true;
  } else if (code == kOneMoreByteCode1) {
    uint8_t result = 0;
    if (!buf->ReadU8(&result)) {
      return OTS_FAILURE();
    }
    *value = result + kLowestUCode;
    return true;
  } else if (code == kOneMoreByteCode2) {
    uint8_t result = 0;
    if (!buf->ReadU8(&result)) {
      return OTS_FAILURE();
    }
    *value = result + kLowestUCode * 2;
    return true;
  } else {
    *value = code;
    return true;
  }
}

bool ReadBase128(ots::Buffer* buf, uint32_t* value) {
  uint32_t result = 0;
  for (size_t i = 0; i < 5; ++i) {
    uint8_t code = 0;
    if (!buf->ReadU8(&code)) {
      return OTS_FAILURE();
    }
    // If any of the top seven bits are set then we're about to overflow.
    if (result & 0xe0000000) {
      return OTS_FAILURE();
    }
    result = (result << 7) | (code & 0x7f);
    if ((code & 0x80) == 0) {
      *value = result;
      return true;
    }
  }
  // Make sure not to exceed the size bound
  return OTS_FAILURE();
}

size_t Base128Size(size_t n) {
  size_t size = 1;
  for (; n >= 128; n >>= 7) ++size;
  return size;
}

void StoreBase128(size_t len, size_t* offset, uint8_t* dst) {
  size_t size = Base128Size(len);
  for (int i = 0; i < size; ++i) {
    int b = (int)(len >> (7 * (size - i - 1))) & 0x7f;
    if (i < size - 1) {
      b |= 0x80;
    }
    dst[(*offset)++] = b;
  }
}

int WithSign(int flag, int baseval) {
  // Precondition: 0 <= baseval < 65536 (to avoid integer overflow)
  return (flag & 1) ? baseval : -baseval;
}

bool TripletDecode(const uint8_t* flags_in, const uint8_t* in, size_t in_size,
    unsigned int n_points, std::vector<Point>* result,
    size_t* in_bytes_consumed) {
  int x = 0;
  int y = 0;

  if (n_points > in_size) {
    return OTS_FAILURE();
  }
  unsigned int triplet_index = 0;

  for (unsigned int i = 0; i < n_points; ++i) {
    uint8_t flag = flags_in[i];
    bool on_curve = !(flag >> 7);
    flag &= 0x7f;
    unsigned int n_data_bytes;
    if (flag < 84) {
      n_data_bytes = 1;
    } else if (flag < 120) {
      n_data_bytes = 2;
    } else if (flag < 124) {
      n_data_bytes = 3;
    } else {
      n_data_bytes = 4;
    }
    if (triplet_index + n_data_bytes > in_size ||
        triplet_index + n_data_bytes < triplet_index) {
      return OTS_FAILURE();
    }
    int dx, dy;
    if (flag < 10) {
      dx = 0;
      dy = WithSign(flag, ((flag & 14) << 7) + in[triplet_index]);
    } else if (flag < 20) {
      dx = WithSign(flag, (((flag - 10) & 14) << 7) + in[triplet_index]);
      dy = 0;
    } else if (flag < 84) {
      int b0 = flag - 20;
      int b1 = in[triplet_index];
      dx = WithSign(flag, 1 + (b0 & 0x30) + (b1 >> 4));
      dy = WithSign(flag >> 1, 1 + ((b0 & 0x0c) << 2) + (b1 & 0x0f));
    } else if (flag < 120) {
      int b0 = flag - 84;
      dx = WithSign(flag, 1 + ((b0 / 12) << 8) + in[triplet_index]);
      dy = WithSign(flag >> 1,
                    1 + (((b0 % 12) >> 2) << 8) + in[triplet_index + 1]);
    } else if (flag < 124) {
      int b2 = in[triplet_index + 1];
      dx = WithSign(flag, (in[triplet_index] << 4) + (b2 >> 4));
      dy = WithSign(flag >> 1, ((b2 & 0x0f) << 8) + in[triplet_index + 2]);
    } else {
      dx = WithSign(flag, (in[triplet_index] << 8) + in[triplet_index + 1]);
      dy = WithSign(flag >> 1,
          (in[triplet_index + 2] << 8) + in[triplet_index + 3]);
    }
    triplet_index += n_data_bytes;
    // Possible overflow but coordinate values are not security sensitive
    x += dx;
    y += dy;
    result->push_back(Point());
    Point& back = result->back();
    back.x = x;
    back.y = y;
    back.on_curve = on_curve;
  }
  *in_bytes_consumed = triplet_index;
  return true;
}

// This function stores just the point data. On entry, dst points to the
// beginning of a simple glyph. Returns true on success.
bool StorePoints(const std::vector<Point>& points,
    unsigned int n_contours, unsigned int instruction_length,
    uint8_t* dst, size_t dst_size, size_t* glyph_size) {
  // I believe that n_contours < 65536, in which case this is safe. However, a
  // comment and/or an assert would be good.
  unsigned int flag_offset = kEndPtsOfContoursOffset + 2 * n_contours + 2 +
    instruction_length;
  int last_flag = -1;
  int repeat_count = 0;
  int last_x = 0;
  int last_y = 0;
  unsigned int x_bytes = 0;
  unsigned int y_bytes = 0;

  for (unsigned int i = 0; i < points.size(); ++i) {
    const Point& point = points[i];
    int flag = point.on_curve ? kGlyfOnCurve : 0;
    int dx = point.x - last_x;
    int dy = point.y - last_y;
    if (dx == 0) {
      flag |= kGlyfThisXIsSame;
    } else if (dx > -256 && dx < 256) {
      flag |= kGlyfXShort | (dx > 0 ? kGlyfThisXIsSame : 0);
      x_bytes += 1;
    } else {
      x_bytes += 2;
    }
    if (dy == 0) {
      flag |= kGlyfThisYIsSame;
    } else if (dy > -256 && dy < 256) {
      flag |= kGlyfYShort | (dy > 0 ? kGlyfThisYIsSame : 0);
      y_bytes += 1;
    } else {
      y_bytes += 2;
    }

    if (flag == last_flag && repeat_count != 255) {
      dst[flag_offset - 1] |= kGlyfRepeat;
      repeat_count++;
    } else {
      if (repeat_count != 0) {
        if (flag_offset >= dst_size) {
          return OTS_FAILURE();
        }
        dst[flag_offset++] = repeat_count;
      }
      if (flag_offset >= dst_size) {
        return OTS_FAILURE();
      }
      dst[flag_offset++] = flag;
      repeat_count = 0;
    }
    last_x = point.x;
    last_y = point.y;
    last_flag = flag;
  }

  if (repeat_count != 0) {
    if (flag_offset >= dst_size) {
      return OTS_FAILURE();
    }
    dst[flag_offset++] = repeat_count;
  }
  unsigned int xy_bytes = x_bytes + y_bytes;
  if (xy_bytes < x_bytes ||
      flag_offset + xy_bytes < flag_offset ||
      flag_offset + xy_bytes > dst_size) {
    return OTS_FAILURE();
  }

  int x_offset = flag_offset;
  int y_offset = flag_offset + x_bytes;
  last_x = 0;
  last_y = 0;
  for (unsigned int i = 0; i < points.size(); ++i) {
    int dx = points[i].x - last_x;
    if (dx == 0) {
      // pass
    } else if (dx > -256 && dx < 256) {
      dst[x_offset++] = std::abs(dx);
    } else {
      // will always fit for valid input, but overflow is harmless
      x_offset = Store16(dst, x_offset, dx);
    }
    last_x += dx;
    int dy = points[i].y - last_y;
    if (dy == 0) {
      // pass
    } else if (dy > -256 && dy < 256) {
      dst[y_offset++] = std::abs(dy);
    } else {
      y_offset = Store16(dst, y_offset, dy);
    }
    last_y += dy;
  }
  *glyph_size = y_offset;
  return true;
}

// Compute the bounding box of the coordinates, and store into a glyf buffer.
// A precondition is that there are at least 10 bytes available.
void ComputeBbox(const std::vector<Point>& points, uint8_t* dst) {
  int x_min = 0;
  int y_min = 0;
  int x_max = 0;
  int y_max = 0;

  for (unsigned int i = 0; i < points.size(); ++i) {
    int x = points[i].x;
    int y = points[i].y;
    if (i == 0 || x < x_min) x_min = x;
    if (i == 0 || x > x_max) x_max = x;
    if (i == 0 || y < y_min) y_min = y;
    if (i == 0 || y > y_max) y_max = y;
  }
  size_t offset = 2;
  offset = Store16(dst, offset, x_min);
  offset = Store16(dst, offset, y_min);
  offset = Store16(dst, offset, x_max);
  offset = Store16(dst, offset, y_max);
}

// Process entire bbox stream. This is done as a separate pass to allow for
// composite bbox computations (an optional more aggressive transform).
bool ProcessBboxStream(ots::Buffer* bbox_stream, unsigned int n_glyphs,
    const std::vector<uint32_t>& loca_values, uint8_t* glyf_buf,
    size_t glyf_buf_length) {
  const uint8_t* buf = bbox_stream->buffer();
  if (n_glyphs >= 65536 || loca_values.size() != n_glyphs + 1) {
    return OTS_FAILURE();
  }
  // Safe because n_glyphs is bounded
  unsigned int bitmap_length = ((n_glyphs + 31) >> 5) << 2;
  if (!bbox_stream->Skip(bitmap_length)) {
    return OTS_FAILURE();
  }
  for (unsigned int i = 0; i < n_glyphs; ++i) {
    if (buf[i >> 3] & (0x80 >> (i & 7))) {
      uint32_t loca_offset = loca_values[i];
      if (loca_values[i + 1] - loca_offset < kEndPtsOfContoursOffset) {
        return OTS_FAILURE();
      }
      if (glyf_buf_length < 2 + 10 ||
          loca_offset > glyf_buf_length - 2 - 10) {
        return OTS_FAILURE();
      }
      if (!bbox_stream->Read(glyf_buf + loca_offset + 2, 8)) {
        return OTS_FAILURE();
      }
    }
  }
  return true;
}

bool ProcessComposite(ots::Buffer* composite_stream, uint8_t* dst,
    size_t dst_size, size_t* glyph_size, bool* have_instructions) {
  size_t start_offset = composite_stream->offset();
  bool we_have_instructions = false;

  uint16_t flags = FLAG_MORE_COMPONENTS;
  while (flags & FLAG_MORE_COMPONENTS) {
    if (!composite_stream->ReadU16(&flags)) {
      return OTS_FAILURE();
    }
    we_have_instructions |= (flags & FLAG_WE_HAVE_INSTRUCTIONS) != 0;
    size_t arg_size = 2;  // glyph index
    if (flags & FLAG_ARG_1_AND_2_ARE_WORDS) {
      arg_size += 4;
    } else {
      arg_size += 2;
    }
    if (flags & FLAG_WE_HAVE_A_SCALE) {
      arg_size += 2;
    } else if (flags & FLAG_WE_HAVE_AN_X_AND_Y_SCALE) {
      arg_size += 4;
    } else if (flags & FLAG_WE_HAVE_A_TWO_BY_TWO) {
      arg_size += 8;
    }
    if (!composite_stream->Skip(arg_size)) {
      return OTS_FAILURE();
    }
  }
  size_t composite_glyph_size = composite_stream->offset() - start_offset;
  if (composite_glyph_size + kCompositeGlyphBegin > dst_size) {
    return OTS_FAILURE();
  }
  Store16(dst, 0, 0xffff);  // nContours = -1 for composite glyph
  std::memcpy(dst + kCompositeGlyphBegin,
      composite_stream->buffer() + start_offset,
      composite_glyph_size);
  *glyph_size = kCompositeGlyphBegin + composite_glyph_size;
  *have_instructions = we_have_instructions;
  return true;
}

// Build TrueType loca table
bool StoreLoca(const std::vector<uint32_t>& loca_values, int index_format,
    uint8_t* dst, size_t dst_size) {
  const uint64_t loca_size = loca_values.size();
  const uint64_t offset_size = index_format ? 4 : 2;
  if ((loca_size << 2) >> 2 != loca_size) {
    return OTS_FAILURE();
  }
  if (offset_size * loca_size > dst_size) {
    return OTS_FAILURE();
  }
  size_t offset = 0;
  for (size_t i = 0; i < loca_values.size(); ++i) {
    uint32_t value = loca_values[i];
    if (index_format) {
      offset = StoreU32(dst, offset, value);
    } else {
      offset = Store16(dst, offset, value >> 1);
    }
  }
  return true;
}

// Reconstruct entire glyf table based on transformed original
bool ReconstructGlyf(const uint8_t* data, size_t data_size,
    uint8_t* dst, size_t dst_size,
    uint8_t* loca_buf, size_t loca_size) {
  static const int kNumSubStreams = 7;
  ots::Buffer file(data, data_size);
  uint32_t version;
  std::vector<std::pair<const uint8_t*, size_t> > substreams(kNumSubStreams);

  if (!file.ReadU32(&version)) {
    return OTS_FAILURE();
  }
  uint16_t num_glyphs;
  uint16_t index_format;
  if (!file.ReadU16(&num_glyphs) ||
      !file.ReadU16(&index_format)) {
    return OTS_FAILURE();
  }
  unsigned int offset = (2 + kNumSubStreams) * 4;
  if (offset > data_size) {
    return OTS_FAILURE();
  }
  // Invariant from here on: data_size >= offset
  for (int i = 0; i < kNumSubStreams; ++i) {
    uint32_t substream_size;
    if (!file.ReadU32(&substream_size)) {
      return OTS_FAILURE();
    }
    if (substream_size > data_size - offset) {
      return OTS_FAILURE();
    }
    substreams[i] = std::make_pair(data + offset, substream_size);
    offset += substream_size;
  }
  ots::Buffer n_contour_stream(substreams[0].first, substreams[0].second);
  ots::Buffer n_points_stream(substreams[1].first, substreams[1].second);
  ots::Buffer flag_stream(substreams[2].first, substreams[2].second);
  ots::Buffer glyph_stream(substreams[3].first, substreams[3].second);
  ots::Buffer composite_stream(substreams[4].first, substreams[4].second);
  ots::Buffer bbox_stream(substreams[5].first, substreams[5].second);
  ots::Buffer instruction_stream(substreams[6].first, substreams[6].second);

  std::vector<uint32_t> loca_values(num_glyphs + 1);
  std::vector<unsigned int> n_points_vec;
  std::vector<Point> points;
  uint32_t loca_offset = 0;
  for (unsigned int i = 0; i < num_glyphs; ++i) {
    size_t glyph_size = 0;
    uint16_t n_contours = 0;
    if (!n_contour_stream.ReadU16(&n_contours)) {
      return OTS_FAILURE();
    }
    uint8_t* glyf_dst = dst + loca_offset;
    size_t glyf_dst_size = dst_size - loca_offset;
    if (n_contours == 0xffff) {
      // composite glyph
      bool have_instructions = false;
      unsigned int instruction_size = 0;
      if (!ProcessComposite(&composite_stream, glyf_dst, glyf_dst_size,
            &glyph_size, &have_instructions)) {
        return OTS_FAILURE();
      }
      if (have_instructions) {
        if (!Read255UShort(&glyph_stream, &instruction_size)) {
          return OTS_FAILURE();
        }
        if (instruction_size + 2 > glyf_dst_size - glyph_size) {
          return OTS_FAILURE();
        }
        Store16(glyf_dst, glyph_size, instruction_size);
        if (!instruction_stream.Read(glyf_dst + glyph_size + 2,
              instruction_size)) {
          return OTS_FAILURE();
        }
        glyph_size += instruction_size + 2;
      }
    } else if (n_contours > 0) {
      // simple glyph
      n_points_vec.clear();
      points.clear();
      unsigned int total_n_points = 0;
      unsigned int n_points_contour;
      for (unsigned int j = 0; j < n_contours; ++j) {
        if (!Read255UShort(&n_points_stream, &n_points_contour)) {
          return OTS_FAILURE();
        }
        n_points_vec.push_back(n_points_contour);
        if (total_n_points + n_points_contour < total_n_points) {
          return OTS_FAILURE();
        }
        total_n_points += n_points_contour;
      }
      unsigned int flag_size = total_n_points;
      if (flag_size > flag_stream.length() - flag_stream.offset()) {
        return OTS_FAILURE();
      }
      const uint8_t* flags_buf = flag_stream.buffer() + flag_stream.offset();
      const uint8_t* triplet_buf = glyph_stream.buffer() +
        glyph_stream.offset();
      size_t triplet_size = glyph_stream.length() - glyph_stream.offset();
      size_t triplet_bytes_consumed = 0;
      if (!TripletDecode(flags_buf, triplet_buf, triplet_size, total_n_points,
            &points, &triplet_bytes_consumed)) {
        return OTS_FAILURE();
      }
      const uint32_t header_and_endpts_contours_size =
          kEndPtsOfContoursOffset + 2 * n_contours;
      if (glyf_dst_size < header_and_endpts_contours_size) {
        return OTS_FAILURE();
      }
      Store16(glyf_dst, 0, n_contours);
      ComputeBbox(points, glyf_dst);
      size_t offset = kEndPtsOfContoursOffset;
      int end_point = -1;
      for (unsigned int contour_ix = 0; contour_ix < n_contours; ++contour_ix) {
        end_point += n_points_vec[contour_ix];
        if (end_point >= 65536) {
          return OTS_FAILURE();
        }
        offset = Store16(glyf_dst, offset, end_point);
      }
      if (!flag_stream.Skip(flag_size)) {
        return OTS_FAILURE();
      }
      if (!glyph_stream.Skip(triplet_bytes_consumed)) {
        return OTS_FAILURE();
      }
      unsigned int instruction_size;
      if (!Read255UShort(&glyph_stream, &instruction_size)) {
        return OTS_FAILURE();
      }
      if (glyf_dst_size - header_and_endpts_contours_size <
          instruction_size + 2) {
        return OTS_FAILURE();
      }
      uint8_t* instruction_dst = glyf_dst + header_and_endpts_contours_size;
      Store16(instruction_dst, 0, instruction_size);
      if (!instruction_stream.Read(instruction_dst + 2, instruction_size)) {
        return OTS_FAILURE();
      }
      if (!StorePoints(points, n_contours, instruction_size,
            glyf_dst, glyf_dst_size, &glyph_size)) {
        return OTS_FAILURE();
      }
    } else {
      glyph_size = 0;
    }
    loca_values[i] = loca_offset;
    if (glyph_size + 3 < glyph_size) {
      return OTS_FAILURE();
    }
    glyph_size = Round4(glyph_size);
    if (glyph_size > dst_size - loca_offset) {
      // This shouldn't happen, but this test defensively maintains the
      // invariant that loca_offset <= dst_size.
      return OTS_FAILURE();
    }
    loca_offset += glyph_size;
  }
  loca_values[num_glyphs] = loca_offset;
  if (!ProcessBboxStream(&bbox_stream, num_glyphs, loca_values,
          dst, dst_size)) {
    return OTS_FAILURE();
  }
  return StoreLoca(loca_values, index_format, loca_buf, loca_size);
}

// This is linear search, but could be changed to binary because we
// do have a guarantee that the tables are sorted by tag. But the total
// cpu time is expected to be very small in any case.
const Table* FindTable(const std::vector<Table>& tables, uint32_t tag) {
  size_t n_tables = tables.size();
  for (size_t i = 0; i < n_tables; ++i) {
    if (tables[i].tag == tag) {
      return &tables[i];
    }
  }
  return NULL;
}

bool ReconstructTransformed(const std::vector<Table>& tables, uint32_t tag,
    const uint8_t* transformed_buf, size_t transformed_size,
    uint8_t* dst, size_t dst_length) {
  if (tag == TAG('g', 'l', 'y', 'f')) {
    const Table* glyf_table = FindTable(tables, tag);
    const Table* loca_table = FindTable(tables, TAG('l', 'o', 'c', 'a'));
    if (glyf_table == NULL || loca_table == NULL) {
      return OTS_FAILURE();
    }
    if (static_cast<uint64_t>(glyf_table->dst_offset + glyf_table->dst_length) >
        dst_length) {
      return OTS_FAILURE();
    }
    if (static_cast<uint64_t>(loca_table->dst_offset + loca_table->dst_length) >
        dst_length) {
      return OTS_FAILURE();
    }
    return ReconstructGlyf(transformed_buf, transformed_size,
        dst + glyf_table->dst_offset, glyf_table->dst_length,
        dst + loca_table->dst_offset, loca_table->dst_length);
  } else if (tag == TAG('l', 'o', 'c', 'a')) {
    // processing was already done by glyf table, but validate
    if (!FindTable(tables, TAG('g', 'l', 'y', 'f'))) {
      return OTS_FAILURE();
    }
  } else {
    // transform for the tag is not known
    return OTS_FAILURE();
  }
  return true;
}

uint32_t ComputeChecksum(const uint8_t* buf, size_t size) {
  uint32_t checksum = 0;
  for (size_t i = 0; i < size; i += 4) {
    // We assume the addition is mod 2^32, which is valid because unsigned
    checksum += (buf[i] << 24) | (buf[i + 1] << 16) |
      (buf[i + 2] << 8) | buf[i + 3];
  }
  return checksum;
}

bool FixChecksums(const std::vector<Table>& tables, uint8_t* dst) {
  const Table* head_table = FindTable(tables, TAG('h', 'e', 'a', 'd'));
  if (head_table == NULL ||
      head_table->dst_length < kCheckSumAdjustmentOffset + 4) {
    return OTS_FAILURE();
  }
  size_t adjustment_offset = head_table->dst_offset + kCheckSumAdjustmentOffset;
  StoreU32(dst, adjustment_offset, 0);
  size_t n_tables = tables.size();
  uint32_t file_checksum = 0;
  for (size_t i = 0; i < n_tables; ++i) {
    const Table* table = &tables[i];
    size_t table_length = table->dst_length;
    uint8_t* table_data = dst + table->dst_offset;
    uint32_t checksum = ComputeChecksum(table_data, table_length);
    StoreU32(dst, kSfntHeaderSize + i * kSfntEntrySize + 4, checksum);
    file_checksum += checksum;
  }
  file_checksum += ComputeChecksum(dst,
      kSfntHeaderSize + kSfntEntrySize * n_tables);
  uint32_t checksum_adjustment = 0xb1b0afba - file_checksum;
  StoreU32(dst, adjustment_offset, checksum_adjustment);
  return true;
}

bool Woff2Compress(const uint8_t* data, const size_t len,
                   uint32_t compression_type,
                   uint8_t* result, uint32_t* result_len) {
  if (compression_type == kCompressionTypeBrotli) {
    size_t compressed_len = *result_len;
    
    brotli::BrotliCompressBuffer(len, data, &compressed_len, result);
    *result_len = compressed_len;
    return true;
  }
  return false;
}

bool Woff2Uncompress(uint8_t* dst_buf, size_t dst_size,
    const uint8_t* src_buf, size_t src_size, uint32_t compression_type) {
  if (compression_type == kCompressionTypeBrotli) {
    size_t uncompressed_size = dst_size;
    int ok = BrotliDecompressBuffer(src_size, src_buf,
                                    &uncompressed_size, dst_buf);
    if (!ok || uncompressed_size != dst_size) {
      return OTS_FAILURE();
    }
    return true;
  }
  // Unknown compression type
  return OTS_FAILURE();
}

bool ReadLongDirectory(ots::Buffer* file, std::vector<Table>* tables,
    size_t num_tables) {
  for (size_t i = 0; i < num_tables; ++i) {
    Table* table = &(*tables)[i];
    if (!file->ReadU32(&table->tag) ||
        !file->ReadU32(&table->flags) ||
        !file->ReadU32(&table->src_length) ||
        !file->ReadU32(&table->transform_length) ||
        !file->ReadU32(&table->dst_length)) {
      return OTS_FAILURE();
    }
  }
  return true;
}

const uint32_t known_tags[29] = {
  TAG('c', 'm', 'a', 'p'),  // 0
  TAG('h', 'e', 'a', 'd'),  // 1
  TAG('h', 'h', 'e', 'a'),  // 2
  TAG('h', 'm', 't', 'x'),  // 3
  TAG('m', 'a', 'x', 'p'),  // 4
  TAG('n', 'a', 'm', 'e'),  // 5
  TAG('O', 'S', '/', '2'),  // 6
  TAG('p', 'o', 's', 't'),  // 7
  TAG('c', 'v', 't', ' '),  // 8
  TAG('f', 'p', 'g', 'm'),  // 9
  TAG('g', 'l', 'y', 'f'),  // 10
  TAG('l', 'o', 'c', 'a'),  // 11
  TAG('p', 'r', 'e', 'p'),  // 12
  TAG('C', 'F', 'F', ' '),  // 13
  TAG('V', 'O', 'R', 'G'),  // 14
  TAG('E', 'B', 'D', 'T'),  // 15
  TAG('E', 'B', 'L', 'C'),  // 16
  TAG('g', 'a', 's', 'p'),  // 17
  TAG('h', 'd', 'm', 'x'),  // 18
  TAG('k', 'e', 'r', 'n'),  // 19
  TAG('L', 'T', 'S', 'H'),  // 20
  TAG('P', 'C', 'L', 'T'),  // 21
  TAG('V', 'D', 'M', 'X'),  // 22
  TAG('v', 'h', 'e', 'a'),  // 23
  TAG('v', 'm', 't', 'x'),  // 24
  TAG('B', 'A', 'S', 'E'),  // 25
  TAG('G', 'D', 'E', 'F'),  // 26
  TAG('G', 'P', 'O', 'S'),  // 27
  TAG('G', 'S', 'U', 'B'),  // 28
};

int KnownTableIndex(uint32_t tag) {
  for (int i = 0; i < 29; ++i) {
    if (tag == known_tags[i]) return i;
  }
  return 31;
}

bool ReadShortDirectory(ots::Buffer* file, std::vector<Table>* tables,
    size_t num_tables) {
  uint32_t last_compression_type = 0;
  for (size_t i = 0; i < num_tables; ++i) {
    Table* table = &(*tables)[i];
    uint8_t flag_byte;
    if (!file->ReadU8(&flag_byte)) {
      return OTS_FAILURE();
    }
    uint32_t tag;
    if ((flag_byte & 0x1f) == 0x1f) {
      if (!file->ReadU32(&tag)) {
        return OTS_FAILURE();
      }
    } else {
      if ((flag_byte & 0x1f) >= (sizeof(known_tags) / sizeof(known_tags[0]))) {
        return OTS_FAILURE();
      }
      tag = known_tags[flag_byte & 0x1f];
    }
    uint32_t flags = flag_byte >> 6;
    if (flags == kShortFlagsContinue) {
      flags = last_compression_type | kWoff2FlagsContinueStream;
    } else {
      if (flags == kCompressionTypeNone ||
          flags == kCompressionTypeGzip ||
          flags == kCompressionTypeLzma) {
        last_compression_type = flags;
      } else {
        return OTS_FAILURE();
      }
    }
    if ((flag_byte & 0x20) != 0) {
      flags |= kWoff2FlagsTransform;
    }
    uint32_t dst_length;
    if (!ReadBase128(file, &dst_length)) {
      return OTS_FAILURE();
    }
    uint32_t transform_length = dst_length;
    if ((flags & kWoff2FlagsTransform) != 0) {
      if (!ReadBase128(file, &transform_length)) {
        return OTS_FAILURE();
      }
    }
    uint32_t src_length = transform_length;
    if ((flag_byte >> 6) == 1 || (flag_byte >> 6) == 2) {
      if (!ReadBase128(file, &src_length)) {
        return OTS_FAILURE();
      }
    } else if ((flag_byte >> 6) == kShortFlagsContinue) {
      // The compressed data for this table is in a previuos table, so we set
      // the src_length to zero.
      src_length = 0;
    }
    table->tag = tag;
    table->flags = flags;
    table->src_length = src_length;
    table->transform_length = transform_length;
    table->dst_length = dst_length;
  }
  return true;
}

}  // namespace

size_t ComputeWOFF2FinalSize(const uint8_t* data, size_t length) {
  ots::Buffer file(data, length);
  uint32_t total_length;

  if (!file.Skip(16) ||
      !file.ReadU32(&total_length)) {
    return 0;
  }
  return total_length;
}

bool ConvertWOFF2ToTTF(uint8_t* result, size_t result_length,
                       const uint8_t* data, size_t length) {
  ots::Buffer file(data, length);

  uint32_t signature;
  uint32_t flavor;
  if (!file.ReadU32(&signature) || signature != kWoff2Signature ||
      !file.ReadU32(&flavor)) {
    return OTS_FAILURE();
  }

  // TODO(user): Should call IsValidVersionTag() here.

  uint32_t reported_length;
  if (!file.ReadU32(&reported_length) || length != reported_length) {
    return OTS_FAILURE();
  }
  uint16_t num_tables;
  if (!file.ReadU16(&num_tables) || !num_tables) {
    return OTS_FAILURE();
  }
  // These reserved bits will be always zero in the final format, but they
  // temporarily indicate the use of brotli, so that we can evaluate gzip, lzma
  // and brotli side-by-side.
  uint16_t reserved;
  if (!file.ReadU16(&reserved)) {
    return OTS_FAILURE();
  }
  // We don't care about these fields of the header:
  //   uint32_t total_sfnt_size
  //   uint16_t major_version, minor_version
  //   uint32_t meta_offset, meta_length, meta_orig_length
  //   uint32_t priv_offset, priv_length
  if (!file.Skip(28)) {
    return OTS_FAILURE();
  }
  std::vector<Table> tables(num_tables);
  // Note: change below to ReadLongDirectory to enable long format.
  if (!ReadShortDirectory(&file, &tables, num_tables)) {
    return OTS_FAILURE();
  }
  uint64_t src_offset = file.offset();
  uint64_t dst_offset = kSfntHeaderSize +
      kSfntEntrySize * static_cast<uint64_t>(num_tables);
  uint64_t uncompressed_sum = 0;
  for (uint16_t i = 0; i < num_tables; ++i) {
    Table* table = &tables[i];
    table->src_offset = src_offset;
    src_offset += table->src_length;
    if (src_offset > std::numeric_limits<uint32_t>::max()) {
      return OTS_FAILURE();
    }
    src_offset = Round4(src_offset);  // TODO: reconsider
    table->dst_offset = dst_offset;
    dst_offset += table->dst_length;
    if (dst_offset > std::numeric_limits<uint32_t>::max()) {
      return OTS_FAILURE();
    }
    dst_offset = Round4(dst_offset);
    if ((table->flags & kCompressionTypeMask) != kCompressionTypeNone) {
      uncompressed_sum += table->src_length;
      if (uncompressed_sum > std::numeric_limits<uint32_t>::max()) {
        return OTS_FAILURE();
      }
    }
  }
  // Enforce same 30M limit on uncompressed tables as OTS
  if (uncompressed_sum > 30 * 1024 * 1024) {
    return OTS_FAILURE();
  }
  if (src_offset > length || dst_offset > result_length) {
    return OTS_FAILURE();
  }

  const uint32_t sfnt_header_and_table_directory_size = 12 + 16 * num_tables;
  if (sfnt_header_and_table_directory_size > result_length) {
    return OTS_FAILURE();
  }

  // Start building the font
  size_t offset = 0;
  offset = StoreU32(result, offset, flavor);
  offset = Store16(result, offset, num_tables);
  unsigned max_pow2 = 0;
  while (1u << (max_pow2 + 1) <= num_tables) {
    max_pow2++;
  }
  const uint16_t output_search_range = (1u << max_pow2) << 4;
  offset = Store16(result, offset, output_search_range);
  offset = Store16(result, offset, max_pow2);
  offset = Store16(result, offset, (num_tables << 4) - output_search_range);
  for (uint16_t i = 0; i < num_tables; ++i) {
    const Table* table = &tables[i];
    offset = StoreU32(result, offset, table->tag);
    offset = StoreU32(result, offset, 0);  // checksum, to fill in later
    offset = StoreU32(result, offset, table->dst_offset);
    offset = StoreU32(result, offset, table->dst_length);
  }
  std::vector<uint8_t> uncompressed_buf;
  bool continue_valid = false;
  const uint8_t* transform_buf = NULL;
  for (uint16_t i = 0; i < num_tables; ++i) {
    const Table* table = &tables[i];
    uint32_t flags = table->flags;
    const uint8_t* src_buf = data + table->src_offset;
    uint32_t compression_type = flags & kCompressionTypeMask;
    if (compression_type == kCompressionTypeLzma && reserved > 0) {
      compression_type = kCompressionTypeLzma + reserved;
    }
    size_t transform_length = table->transform_length;
    if ((flags & kWoff2FlagsContinueStream) != 0) {
      if (!continue_valid) {
        return OTS_FAILURE();
      }
    } else if (compression_type == kCompressionTypeNone) {
      if (transform_length != table->src_length) {
        return OTS_FAILURE();
      }
      transform_buf = src_buf;
      continue_valid = false;
    } else if ((flags & kWoff2FlagsContinueStream) == 0) {
      uint64_t total_size = transform_length;
      for (uint16_t j = i + 1; j < num_tables; ++j) {
        if ((tables[j].flags & kWoff2FlagsContinueStream) == 0) {
          break;
        }
        total_size += tables[j].transform_length;
        if (total_size > std::numeric_limits<uint32_t>::max()) {
          return OTS_FAILURE();
        }
      }
      uncompressed_buf.resize(total_size);
      if (!Woff2Uncompress(&uncompressed_buf[0], total_size,
          src_buf, table->src_length, compression_type)) {
        return OTS_FAILURE();
      }
      transform_buf = &uncompressed_buf[0];
      continue_valid = true;
    } else {
      return OTS_FAILURE();
    }

    if ((flags & kWoff2FlagsTransform) == 0) {
      if (transform_length != table->dst_length) {
        return OTS_FAILURE();
      }
      if (static_cast<uint64_t>(table->dst_offset + transform_length) >
          result_length) {
        return OTS_FAILURE();
      }
      std::memcpy(result + table->dst_offset, transform_buf,
          transform_length);
    } else {
      if (!ReconstructTransformed(tables, table->tag,
            transform_buf, transform_length, result, result_length)) {
        return OTS_FAILURE();
      }
    }
    if (continue_valid) {
      transform_buf += transform_length;
      if (transform_buf > uncompressed_buf.data() + uncompressed_buf.size()) {
        return OTS_FAILURE();
      }
    }
  }

  return FixChecksums(tables, result);
}

void StoreTableEntry(const Table& table, size_t* offset, uint8_t* dst) {
  uint8_t flag_byte = KnownTableIndex(table.tag);
  if ((table.flags & kWoff2FlagsTransform) != 0) {
    flag_byte |= 0x20;
  }
  if ((table.flags & kWoff2FlagsContinueStream) != 0) {
    flag_byte |= 0xc0;
  } else {
    flag_byte |= ((table.flags & 3) << 6);
  }
  dst[(*offset)++] = flag_byte;
  if ((flag_byte & 0x1f) == 0x1f) {
    StoreU32(table.tag, offset, dst);
  }
  StoreBase128(table.src_length, offset, dst);
  if ((flag_byte & 0x20) != 0) {
    StoreBase128(table.transform_length, offset, dst);
  }
  if ((flag_byte & 0xc0) == 0x40 || (flag_byte & 0xc0) == 0x80) {
    StoreBase128(table.dst_length, offset, dst);
  }
}

size_t TableEntrySize(const Table& table) {
  size_t size = KnownTableIndex(table.tag) < 31 ? 1 : 5;
  size += Base128Size(table.src_length);
  if ((table.flags & kWoff2FlagsTransform) != 0) {
    size += Base128Size(table.transform_length);
  }
  if ((table.flags & kWoff2FlagsContinueStream) == 0 &&
      ((table.flags & 3) == kCompressionTypeGzip ||
       (table.flags & 3) == kCompressionTypeLzma)) {
    size += Base128Size(table.dst_length);
  }
  return size;
}

size_t ComputeWoff2Length(const std::vector<Table>& tables) {
  size_t size = 44;  // header size
  for (const auto& table : tables) {
    size += TableEntrySize(table);
  }
  for (const auto& table : tables) {
    size += table.dst_length;
    size = Round4(size);
  }
  return size;
}

size_t ComputeTTFLength(const std::vector<Table>& tables) {
  size_t size = 12 + 16 * tables.size();  // sfnt header
  for (const auto& table : tables) {
    size += Round4(table.src_length);
  }
  return size;
}

size_t ComputeTotalTransformLength(const Font& font) {
  size_t total = 0;
  for (const auto& i : font.tables) {
    const Font::Table& table = i.second;
    if (table.tag & 0x80808080 || !font.FindTable(table.tag ^ 0x80808080)) {
      // Count transformed tables and non-transformed tables that do not have
      // transformed versions.
      total += table.length;
    }
  }
  return total;
}

struct Woff2ConvertOptions {
  uint32_t compression_type;
  bool continue_streams;
  bool keep_dsig;
  bool transform_glyf;

  Woff2ConvertOptions()
      : compression_type(kCompressionTypeBrotli),
        continue_streams(true),
        keep_dsig(true),
        transform_glyf(true) {}


};

size_t MaxWOFF2CompressedSize(const uint8_t* data, size_t length) {
  // Except for the header size, which is 32 bytes larger in woff2 format,
  // all other parts should be smaller (table header in short format,
  // transformations and compression). Just to be sure, we will give some
  // headroom anyway.
  return length + 1024;
}

bool ConvertTTFToWOFF2(const uint8_t *data, size_t length,
                       uint8_t *result, size_t *result_length) {

  Woff2ConvertOptions options;

  Font font;
  if (!ReadFont(data, length, &font)) {
    fprintf(stderr, "Parsing of the input font failed.\n");
    return false;
  }

  if (!NormalizeFont(&font)) {
    fprintf(stderr, "Font normalization failed.\n");
    return false;
  }

  if (!options.keep_dsig) {
    font.tables.erase(TAG('D', 'S', 'I', 'G'));
  }

  if (options.transform_glyf &&
      !TransformGlyfAndLocaTables(&font)) {
    fprintf(stderr, "Font transformation failed.\n");
    return false;
  }

  const Font::Table* head_table = font.FindTable(kHeadTableTag);
  if (head_table == NULL) {
    fprintf(stderr, "Missing head table.\n");
    return false;
  }

  // Although the compressed size of each table in the final woff2 file won't
  // be larger than its transform_length, we have to allocate a large enough
  // buffer for the compressor, since the compressor can potentially increase
  // the size. If the compressor overflows this, it should return false and
  // then this function will also return false.
  size_t total_transform_length = ComputeTotalTransformLength(font);
  size_t compression_buffer_size = 1.2 * total_transform_length + 10240;
  std::vector<uint8_t> compression_buf(compression_buffer_size);
  size_t compression_buf_offset = 0;
  uint32_t total_compressed_length = compression_buffer_size;

  if (options.continue_streams) {
    // Collect all transformed data into one place.
    std::vector<uint8_t> transform_buf(total_transform_length);
    size_t transform_offset = 0;
    for (const auto& i : font.tables) {
      if (i.second.tag & 0x80808080) continue;
      const Font::Table* table = font.FindTable(i.second.tag ^ 0x80808080);
      if (table == NULL) table = &i.second;
      StoreBytes(table->data, table->length,
                 &transform_offset, &transform_buf[0]);
    }
    // Compress all transformed data in one stream.
    if (!Woff2Compress(transform_buf.data(), total_transform_length,
                       options.compression_type,
                       &compression_buf[0],
                       &total_compressed_length)) {
      fprintf(stderr, "Compression of combined table failed.\n");
      return false;
    }
  }

  std::vector<Table> tables;
  for (const auto& i : font.tables) {
    const Font::Table& src_table = i.second;
    if (src_table.tag & 0x80808080) {
      // This is a transformed table, we will write it together with the
      // original version.
      continue;
    }
    Table table;
    table.tag = src_table.tag;
    table.flags = std::min(options.compression_type, kCompressionTypeLzma);
    table.src_length = src_table.length;
    table.transform_length = src_table.length;
    const uint8_t* transformed_data = src_table.data;
    const Font::Table* transformed_table =
        font.FindTable(src_table.tag ^ 0x80808080);
    if (transformed_table != NULL) {
      table.flags |= kWoff2FlagsTransform;
      table.transform_length = transformed_table->length;
      transformed_data = transformed_table->data;
    }
    if (options.continue_streams) {
      if (tables.empty()) {
        table.dst_length = total_compressed_length;
        table.dst_data = &compression_buf[0];
      } else {
        table.dst_length = 0;
        table.dst_data = NULL;
        table.flags |= kWoff2FlagsContinueStream;
      }
    } else {
      table.dst_length = table.transform_length;
      table.dst_data = transformed_data;
      if (options.compression_type != kCompressionTypeNone) {
        uint32_t compressed_length =
            compression_buf.size() - compression_buf_offset;
        if (!Woff2Compress(transformed_data, table.transform_length,
                           options.compression_type,
                           &compression_buf[compression_buf_offset],
                           &compressed_length)) {
          fprintf(stderr, "Compression of table %x failed.\n", src_table.tag);
          return false;
        }
        if (compressed_length >= table.transform_length) {
          table.flags &= (~3);  // no compression
        } else {
          table.dst_length = compressed_length;
          table.dst_data = &compression_buf[compression_buf_offset];
          compression_buf_offset += table.dst_length;
        }
      }
    }
    tables.push_back(table);
  }

  size_t woff2_length = ComputeWoff2Length(tables);
  if (woff2_length > *result_length) {
    fprintf(stderr, "Result allocation was too small (%zd vs %zd bytes).\n",
           *result_length, woff2_length);
    return false;
  }
  *result_length = woff2_length;
  uint16_t reserved =
      (options.compression_type > kCompressionTypeLzma) ?
      options.compression_type - kCompressionTypeLzma : 0;

  size_t offset = 0;
  StoreU32(kWoff2Signature, &offset, result);
  StoreU32(font.flavor, &offset, result);
  StoreU32(woff2_length, &offset, result);
  Store16(tables.size(), &offset, result);
  Store16(reserved, &offset, result);
  StoreU32(ComputeTTFLength(tables), &offset, result);
  StoreBytes(head_table->data + 4, 4, &offset, result);  // font revision
  StoreU32(0, &offset, result);  // metaOffset
  StoreU32(0, &offset, result);  // metaLength
  StoreU32(0, &offset, result);  // metaOrigLength
  StoreU32(0, &offset, result);  // privOffset
  StoreU32(0, &offset, result);  // privLength
  for (const auto& table : tables) {
    StoreTableEntry(table, &offset, result);
  }
  for (const auto& table : tables) {
    StoreBytes(table.dst_data, table.dst_length, &offset, result);
    offset = Round4(offset);
  }
  if (*result_length != offset) {
    fprintf(stderr, "Mismatch between computed and actual length "
            "(%zd vs %zd)\n", *result_length, offset);
    return false;
  }
  return true;
}

} // namespace woff2
