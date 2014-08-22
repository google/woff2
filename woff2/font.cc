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
// Font management utilities

#include "./font.h"

#include <algorithm>

#include "./buffer.h"
#include "./port.h"
#include "./store_bytes.h"
#include "./table_tags.h"

namespace woff2 {

Font::Table* Font::FindTable(uint32_t tag) {
  std::map<uint32_t, Font::Table>::iterator it = tables.find(tag);
  return it == tables.end() ? 0 : &it->second;
}

const Font::Table* Font::FindTable(uint32_t tag) const {
  std::map<uint32_t, Font::Table>::const_iterator it = tables.find(tag);
  return it == tables.end() ? 0 : &it->second;
}

bool ReadFont(const uint8_t* data, size_t len, Font* font) {
  Buffer file(data, len);

  // We don't care about the search_range, entry_selector and range_shift
  // fields, they will always be computed upon writing the font.
  if (!file.ReadU32(&font->flavor) ||
      !file.ReadU16(&font->num_tables) ||
      !file.Skip(6)) {
    return FONT_COMPRESSION_FAILURE();
  }

  std::map<uint32_t, uint32_t> intervals;
  for (uint16_t i = 0; i < font->num_tables; ++i) {
    Font::Table table;
    if (!file.ReadU32(&table.tag) ||
        !file.ReadU32(&table.checksum) ||
        !file.ReadU32(&table.offset) ||
        !file.ReadU32(&table.length)) {
      return FONT_COMPRESSION_FAILURE();
    }
    if ((table.offset & 3) != 0 ||
        table.length > len ||
        len - table.length < table.offset) {
      return FONT_COMPRESSION_FAILURE();
    }
    intervals[table.offset] = table.length;
    table.data = data + table.offset;
    if (font->tables.find(table.tag) != font->tables.end()) {
      return FONT_COMPRESSION_FAILURE();
    }
    font->tables[table.tag] = table;
  }

  // Check that tables are non-overlapping.
  uint32_t last_offset = 12UL + 16UL * font->num_tables;
  for (const auto& i : intervals) {
    if (i.first < last_offset || i.first + i.second < i.first) {
      return FONT_COMPRESSION_FAILURE();
    }
    last_offset = i.first + i.second;
  }
  return true;
}

size_t FontFileSize(const Font& font) {
  size_t max_offset = 12ULL + 16ULL * font.num_tables;
  for (const auto& i : font.tables) {
    const Font::Table& table = i.second;
    size_t padding_size = (4 - (table.length & 3)) & 3;
    size_t end_offset = (padding_size + table.offset) + table.length;
    max_offset = std::max(max_offset, end_offset);
  }
  return max_offset;
}

bool WriteFont(const Font& font, uint8_t* dst, size_t dst_size) {
  if (dst_size < 12ULL + 16ULL * font.num_tables) {
    return FONT_COMPRESSION_FAILURE();
  }
  size_t offset = 0;
  StoreU32(font.flavor, &offset, dst);
  Store16(font.num_tables, &offset, dst);
  uint16_t max_pow2 = font.num_tables ? Log2Floor(font.num_tables) : 0;
  uint16_t search_range = max_pow2 ? 1 << (max_pow2 + 4) : 0;
  uint16_t range_shift = (font.num_tables << 4) - search_range;
  Store16(search_range, &offset, dst);
  Store16(max_pow2, &offset, dst);
  Store16(range_shift, &offset, dst);
  for (const auto& i : font.tables) {
    const Font::Table& table = i.second;
    StoreU32(table.tag, &offset, dst);
    StoreU32(table.checksum, &offset, dst);
    StoreU32(table.offset, &offset, dst);
    StoreU32(table.length, &offset, dst);
    if (table.offset + table.length < table.offset ||
        dst_size < table.offset + table.length) {
      return FONT_COMPRESSION_FAILURE();
    }
    memcpy(dst + table.offset, table.data, table.length);
    size_t padding_size = (4 - (table.length & 3)) & 3;
    if (table.offset + table.length + padding_size < padding_size ||
        dst_size < table.offset + table.length + padding_size) {
      return FONT_COMPRESSION_FAILURE();
    }
    memset(dst + table.offset + table.length, 0, padding_size);
  }
  return true;
}

int NumGlyphs(const Font& font) {
  const Font::Table* head_table = font.FindTable(kHeadTableTag);
  const Font::Table* loca_table = font.FindTable(kLocaTableTag);
  if (head_table == NULL || loca_table == NULL || head_table->length < 52) {
    return 0;
  }
  int index_fmt = IndexFormat(font);
  int num_glyphs = (loca_table->length / (index_fmt == 0 ? 2 : 4)) - 1;
  return num_glyphs;
}

int IndexFormat(const Font& font) {
  const Font::Table* head_table = font.FindTable(kHeadTableTag);
  if (head_table == NULL) {
    return 0;
  }
  return head_table->data[51];
}

bool GetGlyphData(const Font& font, int glyph_index,
                  const uint8_t** glyph_data, size_t* glyph_size) {
  if (glyph_index < 0) {
    return FONT_COMPRESSION_FAILURE();
  }
  const Font::Table* head_table = font.FindTable(kHeadTableTag);
  const Font::Table* loca_table = font.FindTable(kLocaTableTag);
  const Font::Table* glyf_table = font.FindTable(kGlyfTableTag);
  if (head_table == NULL || loca_table == NULL || glyf_table == NULL ||
      head_table->length < 52) {
    return FONT_COMPRESSION_FAILURE();
  }

  int index_fmt = IndexFormat(font);

  Buffer loca_buf(loca_table->data, loca_table->length);
  if (index_fmt == 0) {
    uint16_t offset1, offset2;
    if (!loca_buf.Skip(2 * glyph_index) ||
        !loca_buf.ReadU16(&offset1) ||
        !loca_buf.ReadU16(&offset2) ||
        offset2 < offset1 ||
        2 * offset2 > glyf_table->length) {
      return FONT_COMPRESSION_FAILURE();
    }
    *glyph_data = glyf_table->data + 2 * offset1;
    *glyph_size = 2 * (offset2 - offset1);
  } else {
    uint32_t offset1, offset2;
    if (!loca_buf.Skip(4 * glyph_index) ||
        !loca_buf.ReadU32(&offset1) ||
        !loca_buf.ReadU32(&offset2) ||
        offset2 < offset1 ||
        offset2 > glyf_table->length) {
      return FONT_COMPRESSION_FAILURE();
    }
    *glyph_data = glyf_table->data + offset1;
    *glyph_size = offset2 - offset1;
  }
  return true;
}

bool RemoveDigitalSignature(Font* font) {
  std::map<uint32_t, Font::Table>::iterator it =
      font->tables.find(kDsigTableTag);
  if (it != font->tables.end()) {
    font->tables.erase(it);
    font->num_tables = font->tables.size();
  }
  return true;
}

} // namespace woff2
