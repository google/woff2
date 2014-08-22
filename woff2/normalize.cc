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
// Glyph normalization

#include "./normalize.h"

#include <inttypes.h>
#include <stddef.h>

#include "./buffer.h"
#include "./port.h"
#include "./font.h"
#include "./glyph.h"
#include "./round.h"
#include "./store_bytes.h"
#include "./table_tags.h"

namespace woff2 {

namespace {

void StoreLoca(int index_fmt, uint32_t value, size_t* offset, uint8_t* dst) {
  if (index_fmt == 0) {
    Store16(value >> 1, offset, dst);
  } else {
    StoreU32(value, offset, dst);
  }
}

void NormalizeSimpleGlyphBoundingBox(Glyph* glyph) {
  if (glyph->contours.empty() || glyph->contours[0].empty()) {
    return;
  }
  int16_t x_min = glyph->contours[0][0].x;
  int16_t y_min = glyph->contours[0][0].y;
  int16_t x_max = x_min;
  int16_t y_max = y_min;
  for (const auto& contour : glyph->contours) {
    for (const auto& point : contour) {
      if (point.x < x_min) x_min = point.x;
      if (point.x > x_max) x_max = point.x;
      if (point.y < y_min) y_min = point.y;
      if (point.y > y_max) y_max = point.y;
    }
  }
  glyph->x_min = x_min;
  glyph->y_min = y_min;
  glyph->x_max = x_max;
  glyph->y_max = y_max;
}

}  // namespace

namespace {

bool WriteNormalizedLoca(int index_fmt, int num_glyphs, Font* font) {
  Font::Table* glyf_table = font->FindTable(kGlyfTableTag);
  Font::Table* loca_table = font->FindTable(kLocaTableTag);

  int glyph_sz = index_fmt == 0 ? 2 : 4;
  loca_table->buffer.resize(Round4(num_glyphs + 1) * glyph_sz);
  loca_table->length = (num_glyphs + 1) * glyph_sz;

  uint8_t* glyf_dst = &glyf_table->buffer[0];
  uint8_t* loca_dst = &loca_table->buffer[0];
  uint32_t glyf_offset = 0;
  size_t loca_offset = 0;

  for (int i = 0; i < num_glyphs; ++i) {
    StoreLoca(index_fmt, glyf_offset, &loca_offset, loca_dst);
    Glyph glyph;
    const uint8_t* glyph_data;
    size_t glyph_size;
    if (!GetGlyphData(*font, i, &glyph_data, &glyph_size) ||
        (glyph_size > 0 && !ReadGlyph(glyph_data, glyph_size, &glyph))) {
      return FONT_COMPRESSION_FAILURE();
    }
    NormalizeSimpleGlyphBoundingBox(&glyph);
    size_t glyf_dst_size = glyf_table->buffer.size() - glyf_offset;
    if (!StoreGlyph(glyph, glyf_dst + glyf_offset, &glyf_dst_size)) {
      return FONT_COMPRESSION_FAILURE();
    }
    glyf_dst_size = Round4(glyf_dst_size);
    if (glyf_dst_size > std::numeric_limits<uint32_t>::max() ||
        glyf_offset + static_cast<uint32_t>(glyf_dst_size) < glyf_offset ||
        (index_fmt == 0 && glyf_offset + glyf_dst_size >= (1UL << 17))) {
      return FONT_COMPRESSION_FAILURE();
    }
    glyf_offset += glyf_dst_size;
  }
  if (glyf_offset == 0) {
    return false;
  }

  StoreLoca(index_fmt, glyf_offset, &loca_offset, loca_dst);

  glyf_table->buffer.resize(glyf_offset);
  glyf_table->data = &glyf_table->buffer[0];
  glyf_table->length = glyf_offset;
  loca_table->data = &loca_table->buffer[0];

  return true;
}

}  // namespace

namespace {

bool MakeEditableBuffer(Font* font, int tableTag) {
  Font::Table* table = font->FindTable(tableTag);
  if (table == NULL) {
    return FONT_COMPRESSION_FAILURE();
  }
  int sz = Round4(table->length);
  table->buffer.resize(sz);
  uint8_t* buf = &table->buffer[0];
  memcpy(buf, table->data, sz);
  table->data = buf;
  return true;
}

}  // namespace

bool NormalizeGlyphs(Font* font) {
  Font::Table* cff_table = font->FindTable(kCffTableTag);
  Font::Table* head_table = font->FindTable(kHeadTableTag);
  Font::Table* glyf_table = font->FindTable(kGlyfTableTag);
  Font::Table* loca_table = font->FindTable(kLocaTableTag);
  if (head_table == NULL) {
    return FONT_COMPRESSION_FAILURE();
  }
  // CFF, no loca, no glyf is OK for CFF. If so, don't normalize.
  if (cff_table != NULL && loca_table == NULL && glyf_table == NULL) {
    return true;
  }
  if (loca_table == NULL || glyf_table == NULL) {
    return FONT_COMPRESSION_FAILURE();
  }
  int index_fmt = head_table->data[51];
  int num_glyphs = NumGlyphs(*font);

  // We need to allocate a bit more than its original length for the normalized
  // glyf table, since it can happen that the glyphs in the original table are
  // 2-byte aligned, while in the normalized table they are 4-byte aligned.
  // That gives a maximum of 2 bytes increase per glyph. However, there is no
  // theoretical guarantee that the total size of the flags plus the coordinates
  // is the smallest possible in the normalized version, so we have to allow
  // some general overhead.
  // TODO(user) Figure out some more precise upper bound on the size of
  // the overhead.
  size_t max_normalized_glyf_size = 1.1 * glyf_table->length + 2 * num_glyphs;

  glyf_table->buffer.resize(max_normalized_glyf_size);

  // if we can't write a loca using short's (index_fmt 0)
  // try again using longs (index_fmt 1)
  if (!WriteNormalizedLoca(index_fmt, num_glyphs, font)) {
    if (index_fmt != 0) {
      return FONT_COMPRESSION_FAILURE();
    }

    // Rewrite loca with 4-byte entries & update head to match
    index_fmt = 1;
    if (!WriteNormalizedLoca(index_fmt, num_glyphs, font)) {
      return FONT_COMPRESSION_FAILURE();
    }
    head_table->buffer[51] = 1;
  }

  return true;
}

bool NormalizeOffsets(Font* font) {
  uint32_t offset = 12 + 16 * font->num_tables;
  for (auto& i : font->tables) {
    i.second.offset = offset;
    offset += Round4(i.second.length);
  }
  return true;
}

namespace {

uint32_t ComputeChecksum(const uint8_t* buf, size_t size) {
  uint32_t checksum = 0;
  for (size_t i = 0; i < size; i += 4) {
    checksum += ((buf[i] << 24) |
                 (buf[i + 1] << 16) |
                 (buf[i + 2] << 8) |
                 buf[i + 3]);
  }
  return checksum;
}

uint32_t ComputeHeaderChecksum(const Font& font) {
  uint32_t checksum = font.flavor;
  uint16_t max_pow2 = font.num_tables ? Log2Floor(font.num_tables) : 0;
  uint16_t search_range = max_pow2 ? 1 << (max_pow2 + 4) : 0;
  uint16_t range_shift = (font.num_tables << 4) - search_range;
  checksum += (font.num_tables << 16 | search_range);
  checksum += (max_pow2 << 16 | range_shift);
  for (const auto& i : font.tables) {
    checksum += i.second.tag;
    checksum += i.second.checksum;
    checksum += i.second.offset;
    checksum += i.second.length;
  }
  return checksum;
}

}  // namespace

bool FixChecksums(Font* font) {
  Font::Table* head_table = font->FindTable(kHeadTableTag);
  if (head_table == NULL || head_table->length < 12) {
    return FONT_COMPRESSION_FAILURE();
  }
  uint8_t* head_buf = &head_table->buffer[0];
  size_t offset = 8;
  StoreU32(0, &offset, head_buf);
  uint32_t file_checksum = 0;
  for (auto& i : font->tables) {
    Font::Table* table = &i.second;
    table->checksum = ComputeChecksum(table->data, table->length);
    file_checksum += table->checksum;
  }
  file_checksum += ComputeHeaderChecksum(*font);
  offset = 8;
  StoreU32(0xb1b0afba - file_checksum, &offset, head_buf);
  return true;
}

bool NormalizeFont(Font* font) {
  return (MakeEditableBuffer(font, kHeadTableTag) &&
          RemoveDigitalSignature(font) &&
          NormalizeGlyphs(font) &&
          NormalizeOffsets(font) &&
          FixChecksums(font));
}

} // namespace woff2
