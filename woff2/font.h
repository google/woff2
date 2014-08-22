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
// Data model for a font file in sfnt format, reading and writing functions and
// accessors for the glyph data.

#ifndef WOFF2_FONT_H_
#define WOFF2_FONT_H_

#include <stddef.h>
#include <inttypes.h>
#include <map>
#include <vector>

namespace woff2 {

// Represents an sfnt font file. Only the table directory is parsed, for the
// table data we only store a raw pointer, therefore a font object is valid only
// as long the data from which it was parsed is around.
struct Font {
  uint32_t flavor;
  uint16_t num_tables;

  struct Table {
    uint32_t tag;
    uint32_t checksum;
    uint32_t offset;
    uint32_t length;
    const uint8_t* data;

    // Buffer used to mutate the data before writing out.
    std::vector<uint8_t> buffer;
  };
  std::map<uint32_t, Table> tables;

  Table* FindTable(uint32_t tag);
  const Table* FindTable(uint32_t tag) const;
};

// Parses the font from the given data. Returns false on parsing failure or
// buffer overflow. The font is valid only so long the input data pointer is
// valid.
bool ReadFont(const uint8_t* data, size_t len, Font* font);

// Returns the file size of the font.
size_t FontFileSize(const Font& font);

// Writes the font into the specified dst buffer. The dst_size should be the
// same as returned by FontFileSize(). Returns false upon buffer overflow (which
// should not happen if dst_size was computed by FontFileSize()).
bool WriteFont(const Font& font, uint8_t* dst, size_t dst_size);

// Returns the number of glyphs in the font.
// NOTE: Currently this works only for TrueType-flavored fonts, will return
// zero for CFF-flavored fonts.
int NumGlyphs(const Font& font);

// Returns the index format of the font
int IndexFormat(const Font& font);

// Sets *glyph_data and *glyph_size to point to the location of the glyph data
// with the given index. Returns false if the glyph is not found.
bool GetGlyphData(const Font& font, int glyph_index,
                  const uint8_t** glyph_data, size_t* glyph_size);

// Removes the digital signature (DSIG) table
bool RemoveDigitalSignature(Font* font);

} // namespace woff2

#endif  // WOFF2_FONT_H_
