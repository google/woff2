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
#include "./variable_length.h"
#include "./woff2_common.h"

namespace woff2 {

namespace {

using std::string;
using std::vector;


const size_t kWoff2HeaderSize = 48;
const size_t kWoff2EntrySize = 20;

bool Compress(const uint8_t* data, const size_t len,
              uint8_t* result, uint32_t* result_len,
              brotli::BrotliParams::Mode mode, int quality) {
  size_t compressed_len = *result_len;
  brotli::BrotliParams params;
  params.mode = mode;
  params.quality = quality;
  if (brotli::BrotliCompressBuffer(params, len, data, &compressed_len, result)
      == 0) {
    return false;
  }
  *result_len = compressed_len;
  return true;
}

bool Woff2Compress(const uint8_t* data, const size_t len,
                   uint8_t* result, uint32_t* result_len, int quality) {
  return Compress(data, len, result, result_len,
                  brotli::BrotliParams::MODE_FONT, quality);
}

bool TextCompress(const uint8_t* data, const size_t len,
                   uint8_t* result, uint32_t* result_len, int quality) {
  return Compress(data, len, result, result_len,
                  brotli::BrotliParams::MODE_TEXT, quality);
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

size_t ComputeWoff2Length(const FontCollection& font_collection,
                          const std::vector<Table>& tables,
                          std::map<uint32_t, uint16_t> index_by_offset,
                          size_t extended_metadata_length) {
  size_t size = kWoff2HeaderSize;

  for (const auto& table : tables) {
    size += TableEntrySize(table);
  }

  // for collections only, collection tables
  if (font_collection.fonts.size() > 1) {
    size += 4;  // UInt32 Version of TTC Header
    size += Size255UShort(font_collection.fonts.size());  // 255UInt16 numFonts

    size += 4 * font_collection.fonts.size();  // UInt32 flavor for each

    for (const auto& font : font_collection.fonts) {
      size += Size255UShort(font.tables.size());  // 255UInt16 numTables
      for (const auto& entry : font.tables) {
        const Font::Table& table = entry.second;
        // no collection entry for xform table
        if (table.tag & 0x80808080) continue;

        uint16_t table_index = index_by_offset[table.offset];
        size += Size255UShort(table_index);  // 255UInt16 index entry
      }
    }
  }

  // compressed data
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

size_t ComputeUncompressedLength(const Font& font) {
  // sfnt header + offset table
  size_t size = 12 + 16 * font.num_tables;
  for (const auto& entry : font.tables) {
    const Font::Table& table = entry.second;
    if (table.tag & 0x80808080) continue;  // xform tables don't stay
    if (table.IsReused()) continue;  // don't have to pay twice
    size += Round4(table.length);
  }
  return size;
}

size_t ComputeUncompressedLength(const FontCollection& font_collection) {
  if (font_collection.fonts.size() == 1) {
    return ComputeUncompressedLength(font_collection.fonts[0]);
  }
  size_t size = CollectionHeaderSize(font_collection.header_version,
    font_collection.fonts.size());
  for (const auto& font : font_collection.fonts) {
    size += ComputeUncompressedLength(font);
  }
  return size;
}

size_t ComputeTotalTransformLength(const Font& font) {
  size_t total = 0;
  for (const auto& i : font.tables) {
    const Font::Table& table = i.second;
    if (table.IsReused()) {
      continue;
    }
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
  return ConvertTTFToWOFF2(data, length, result, result_length, 11, "");
}

bool ConvertTTFToWOFF2(const uint8_t *data, size_t length,
                       uint8_t *result, size_t *result_length,
                       const string& extended_metadata) {
  return ConvertTTFToWOFF2(data, length, result, result_length, 11, extended_metadata);
}

bool TransformFontCollection(FontCollection* font_collection) {
  for (auto& font : font_collection->fonts) {
    if (!TransformGlyfAndLocaTables(&font)) {
      fprintf(stderr, "Font transformation failed.\n");
      return false;
    }
  }

  return true;
}

bool ConvertTTFToWOFF2(const uint8_t *data, size_t length,
                       uint8_t *result, size_t *result_length,
                       int quality, const string& extended_metadata) {
  FontCollection font_collection;
  if (!ReadFontCollection(data, length, &font_collection)) {
    fprintf(stderr, "Parsing of the input font failed.\n");
    return false;
  }

  if (!NormalizeFontCollection(&font_collection)) {
    return false;
  }

  if (!TransformFontCollection(&font_collection)) {
    return false;
  }

  // Although the compressed size of each table in the final woff2 file won't
  // be larger than its transform_length, we have to allocate a large enough
  // buffer for the compressor, since the compressor can potentially increase
  // the size. If the compressor overflows this, it should return false and
  // then this function will also return false.

  size_t total_transform_length = 0;
  for (const auto& font : font_collection.fonts) {
    total_transform_length += ComputeTotalTransformLength(font);
  }
  size_t compression_buffer_size = CompressedBufferSize(total_transform_length);
  std::vector<uint8_t> compression_buf(compression_buffer_size);
  uint32_t total_compressed_length = compression_buffer_size;

  // Collect all transformed data into one place.
  std::vector<uint8_t> transform_buf(total_transform_length);
  size_t transform_offset = 0;
  for (const auto& font : font_collection.fonts) {
    for (const auto& i : font.tables) {
      const Font::Table* table = font.FindTable(i.second.tag ^ 0x80808080);
      if (i.second.IsReused()) continue;
      if (i.second.tag & 0x80808080) continue;

      if (table == NULL) table = &i.second;
      StoreBytes(table->data, table->length,
                 &transform_offset, &transform_buf[0]);
    }
  }
  // Compress all transformed data in one stream.
  if (!Woff2Compress(transform_buf.data(), total_transform_length,
                     &compression_buf[0],
                     &total_compressed_length, quality)) {
    fprintf(stderr, "Compression of combined table failed.\n");
    return false;
  }

  // Compress the extended metadata
  // TODO(user): how does this apply to collections
  uint32_t compressed_metadata_buf_length =
    CompressedBufferSize(extended_metadata.length());
  std::vector<uint8_t> compressed_metadata_buf(compressed_metadata_buf_length);

  if (extended_metadata.length() > 0) {
    if (!TextCompress((const uint8_t*)extended_metadata.data(),
                      extended_metadata.length(),
                      compressed_metadata_buf.data(),
                      &compressed_metadata_buf_length, quality)) {
      fprintf(stderr, "Compression of extended metadata failed.\n");
      return false;
    }
  } else {
    compressed_metadata_buf_length = 0;
  }

  std::vector<Table> tables;
  std::map<uint32_t, uint16_t> index_by_offset;

  for (const auto& font : font_collection.fonts) {

    for (const auto tag : font.OutputOrderedTags()) {
      const Font::Table& src_table = font.tables.at(tag);
      if (src_table.IsReused()) {
        continue;
      }

      if (index_by_offset.find(src_table.offset) == index_by_offset.end()) {
        index_by_offset[src_table.offset] = tables.size();
      } else {
        return false;
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
  }

  size_t woff2_length = ComputeWoff2Length(font_collection, tables,
      index_by_offset, compressed_metadata_buf_length);
  if (woff2_length > *result_length) {
    fprintf(stderr, "Result allocation was too small (%zd vs %zd bytes).\n",
           *result_length, woff2_length);
    return false;
  }
  *result_length = woff2_length;

  const Font& first_font = font_collection.fonts[0];
  size_t offset = 0;

  // start of woff2 header (http://www.w3.org/TR/WOFF2/#woff20Header)
  StoreU32(kWoff2Signature, &offset, result);
  if (font_collection.fonts.size() == 1) {
    StoreU32(first_font.flavor, &offset, result);
  } else {
    StoreU32(kTtcFontFlavor, &offset, result);
  }
  StoreU32(woff2_length, &offset, result);
  Store16(tables.size(), &offset, result);
  Store16(0, &offset, result);  // reserved
  // totalSfntSize
  StoreU32(ComputeUncompressedLength(font_collection), &offset, result);
  StoreU32(total_compressed_length, &offset, result);  // totalCompressedSize

  // TODO(user): is always taking this from the first tables head OK?
  // font revision
  StoreBytes(first_font.FindTable(kHeadTableTag)->data + 4, 4, &offset, result);
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
  // end of woff2 header

  // table directory (http://www.w3.org/TR/WOFF2/#table_dir_format)
  for (const auto& table : tables) {
    StoreTableEntry(table, &offset, result);
  }

  // for collections only, collection table directory
  if (font_collection.fonts.size() > 1) {
    StoreU32(font_collection.header_version, &offset, result);
    Store255UShort(font_collection.fonts.size(), &offset, result);
    for (const Font& font : font_collection.fonts) {

      uint16_t num_tables = 0;
      for (const auto& entry : font.tables) {
        const Font::Table& table = entry.second;
        if (table.tag & 0x80808080) continue;  // don't write xform tables
        num_tables++;
      }
      Store255UShort(num_tables, &offset, result);

      StoreU32(font.flavor, &offset, result);
      for (const auto& entry : font.tables) {
        const Font::Table& table = entry.second;
        if (table.tag & 0x80808080) continue;  // don't write xform tables

        // for reused tables, only the original has an updated offset
        uint32_t table_offset =
          table.IsReused() ? table.reuse_of->offset : table.offset;
        uint32_t table_length =
          table.IsReused() ? table.reuse_of->length : table.length;
        if (index_by_offset.find(table_offset) == index_by_offset.end()) {
          fprintf(stderr, "Missing table index for offset 0x%08x\n",
                  table_offset);
          return false;
        }
        uint16_t index = index_by_offset[table_offset];
        Store255UShort(index, &offset, result);

      }

    }
  }

  // compressed data format (http://www.w3.org/TR/WOFF2/#table_format)
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
