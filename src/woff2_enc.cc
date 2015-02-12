// Copyright 2014 Google Inc. All Rights Reserved.
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
// Library for converting TTF format font files to their WOFF2 versions.

#include "./woff2_enc.h"

#include <stdlib.h>
#include <complex>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "./buffer.h"
#include "./encode.h"
#include "./font.h"
#include "./normalize.h"
#include "./round.h"
#include "./store_bytes.h"
#include "./table_tags.h"
#include "./transform.h"
#include "./woff2_common.h"

namespace woff2 {

namespace {

using std::string;
using std::vector;


const size_t kWoff2HeaderSize = 48;
const size_t kWoff2EntrySize = 20;

size_t Base128Size(size_t n) {
  size_t size = 1;
  for (; n >= 128; n >>= 7) ++size;
  return size;
}

void StoreBase128(size_t len, size_t* offset, uint8_t* dst) {
  size_t size = Base128Size(len);
  for (int i = 0; i < size; ++i) {
    int b = static_cast<int>((len >> (7 * (size - i - 1))) & 0x7f);
    if (i < size - 1) {
      b |= 0x80;
    }
    dst[(*offset)++] = b;
  }
}

bool Compress(const uint8_t* data, const size_t len,
              uint8_t* result, uint32_t* result_len,
              brotli::BrotliParams::Mode mode) {
  size_t compressed_len = *result_len;
  brotli::BrotliParams params;
  params.mode = mode;
  if (brotli::BrotliCompressBuffer(params, len, data, &compressed_len, result)
      == 0) {
    return false;
  }
  *result_len = compressed_len;
  return true;
}

bool Woff2Compress(const uint8_t* data, const size_t len,
                   uint8_t* result, uint32_t* result_len) {
  return Compress(data, len, result, result_len,
                  brotli::BrotliParams::MODE_FONT);
}

bool TextCompress(const uint8_t* data, const size_t len,
                   uint8_t* result, uint32_t* result_len) {
  return Compress(data, len, result, result_len,
                  brotli::BrotliParams::MODE_TEXT);
}

bool ReadLongDirectory(Buffer* file, std::vector<Table>* tables,
    size_t num_tables) {
  for (size_t i = 0; i < num_tables; ++i) {
    Table* table = &(*tables)[i];
    if (!file->ReadU32(&table->tag) ||
        !file->ReadU32(&table->flags) ||
        !file->ReadU32(&table->src_length) ||
        !file->ReadU32(&table->transform_length) ||
        !file->ReadU32(&table->dst_length)) {
      return FONT_COMPRESSION_FAILURE();
    }
  }
  return true;
}

int KnownTableIndex(uint32_t tag) {
  for (int i = 0; i < 63; ++i) {
    if (tag == kKnownTags[i]) return i;
  }
  return 63;
}

void StoreTableEntry(const Table& table, size_t* offset, uint8_t* dst) {
  uint8_t flag_byte = KnownTableIndex(table.tag);
  dst[(*offset)++] = flag_byte;
  // The index here is treated as a set of flag bytes because
  // bits 6 and 7 of the byte are reserved for future use as flags.
  // 0x3f or 63 means an arbitrary table tag.
  if ((flag_byte & 0x3f) == 0x3f) {
    StoreU32(table.tag, offset, dst);
  }
  StoreBase128(table.src_length, offset, dst);
  if ((table.flags & kWoff2FlagsTransform) != 0) {
    StoreBase128(table.transform_length, offset, dst);
  }
}

size_t TableEntrySize(const Table& table) {
  uint8_t flag_byte = KnownTableIndex(table.tag);
  size_t size = ((flag_byte & 0x3f) != 0x3f) ? 1 : 5;
  size += Base128Size(table.src_length);
  if ((table.flags & kWoff2FlagsTransform) != 0) {
     size += Base128Size(table.transform_length);
  }
  return size;
}

size_t ComputeWoff2Length(const std::vector<Table>& tables,
                          size_t extended_metadata_length) {
  size_t size = kWoff2HeaderSize;
  for (const auto& table : tables) {
    size += TableEntrySize(table);
  }
  for (const auto& table : tables) {
    size += table.dst_length;
    size = Round4(size);
  }
  size += extended_metadata_length;
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

}  // namespace

size_t MaxWOFF2CompressedSize(const uint8_t* data, size_t length) {
  return MaxWOFF2CompressedSize(data, length, "");
}

size_t MaxWOFF2CompressedSize(const uint8_t* data, size_t length,
    const string& extended_metadata) {
  // Except for the header size, which is 32 bytes larger in woff2 format,
  // all other parts should be smaller (table header in short format,
  // transformations and compression). Just to be sure, we will give some
  // headroom anyway.
  return length + 1024 + extended_metadata.length();
}

uint32_t CompressedBufferSize(uint32_t original_size) {
  return 1.2 * original_size + 10240;
}

bool ConvertTTFToWOFF2(const uint8_t *data, size_t length,
                       uint8_t *result, size_t *result_length) {
  return ConvertTTFToWOFF2(data, length, result, result_length, "");
}

bool ConvertTTFToWOFF2(const uint8_t *data, size_t length,
                       uint8_t *result, size_t *result_length,
                       const string& extended_metadata) {
  Font font;
  if (!ReadFont(data, length, &font)) {
    fprintf(stderr, "Parsing of the input font failed.\n");
    return false;
  }

  if (!NormalizeFont(&font)) {
    fprintf(stderr, "Font normalization failed.\n");
    return false;
  }

  if (!TransformGlyfAndLocaTables(&font)) {
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
  size_t compression_buffer_size = CompressedBufferSize(total_transform_length);
  std::vector<uint8_t> compression_buf(compression_buffer_size);
  uint32_t total_compressed_length = compression_buffer_size;

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
                     &compression_buf[0],
                     &total_compressed_length)) {
    fprintf(stderr, "Compression of combined table failed.\n");
    return false;
  }

  // Compress the extended metadata
  uint32_t compressed_metadata_buf_length =
    CompressedBufferSize(extended_metadata.length());
  std::vector<uint8_t> compressed_metadata_buf(compressed_metadata_buf_length);

  if (extended_metadata.length() > 0) {
    if (!TextCompress((const uint8_t*)extended_metadata.data(),
                      extended_metadata.length(),
                      compressed_metadata_buf.data(),
                      &compressed_metadata_buf_length)) {
      fprintf(stderr, "Compression of extended metadata failed.\n");
      return false;
    }
  } else {
    compressed_metadata_buf_length = 0;
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
    table.flags = 0;
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
    if (tables.empty()) {
      table.dst_length = total_compressed_length;
      table.dst_data = &compression_buf[0];
    } else {
      table.dst_length = 0;
      table.dst_data = NULL;
      table.flags |= kWoff2FlagsContinueStream;
    }
    tables.push_back(table);
  }

  size_t woff2_length =
    ComputeWoff2Length(tables, compressed_metadata_buf_length);
  if (woff2_length > *result_length) {
    fprintf(stderr, "Result allocation was too small (%zd vs %zd bytes).\n",
           *result_length, woff2_length);
    return false;
  }
  *result_length = woff2_length;

  size_t offset = 0;
  StoreU32(kWoff2Signature, &offset, result);
  StoreU32(font.flavor, &offset, result);
  StoreU32(woff2_length, &offset, result);
  Store16(tables.size(), &offset, result);
  Store16(0, &offset, result);  // reserved
  StoreU32(ComputeTTFLength(tables), &offset, result);
  StoreU32(total_compressed_length, &offset, result);
  StoreBytes(head_table->data + 4, 4, &offset, result);  // font revision
  if (compressed_metadata_buf_length > 0) {
    StoreU32(woff2_length - compressed_metadata_buf_length,
             &offset, result);  // metaOffset
    StoreU32(compressed_metadata_buf_length, &offset, result);  // metaLength
    StoreU32(extended_metadata.length(), &offset, result);  // metaOrigLength
  } else {
    StoreU32(0, &offset, result);  // metaOffset
    StoreU32(0, &offset, result);  // metaLength
    StoreU32(0, &offset, result);  // metaOrigLength
  }
  StoreU32(0, &offset, result);  // privOffset
  StoreU32(0, &offset, result);  // privLength
  for (const auto& table : tables) {
    StoreTableEntry(table, &offset, result);
  }
  for (const auto& table : tables) {
    StoreBytes(table.dst_data, table.dst_length, &offset, result);
    offset = Round4(offset);
  }
  StoreBytes(compressed_metadata_buf.data(), compressed_metadata_buf_length,
             &offset, result);

  if (*result_length != offset) {
    fprintf(stderr, "Mismatch between computed and actual length "
            "(%zd vs %zd)\n", *result_length, offset);
    return false;
  }
  return true;
}

} // namespace woff2
