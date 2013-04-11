// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.data.ReadableFontData;
import com.google.typography.font.sfntly.data.WritableFontData;
import com.google.typography.font.sfntly.table.Table;

/**
 * Encoder for "kern" table. This probably won't go in the spec because an even more
 * effective technique would be to do class kerning in the GDEF tables, but, even so, I wanted
 * to capture the stats.
 *
 * @author raph@google.com (Raph Levien)
 */
public class KernEncoder {

  public static WritableFontData encode(Font font) {
    Table kernTable = font.getTable(Tag.kern);
    ReadableFontData data = kernTable.readFontData();
    WritableFontData newData = WritableFontData.createWritableFontData(data.size());
    data.copyTo(newData);
    if (data.readUShort(0) == 0 && data.readUShort(4) == 0) {
      int base = 18;
      int nPairs = data.readUShort(10);
      for (int i = 0; i < nPairs; i++) {
        newData.writeUShort(base + i * 2, data.readUShort(base + i * 6));
        newData.writeUShort(base + nPairs * 2 + i * 2, data.readUShort(base + i * 6 + 2));
        newData.writeUShort(base + nPairs * 4 + i * 2, data.readUShort(base + i * 6 + 4));
      }
    }
    return newData;
  }
}
