// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.common.collect.Lists;
import com.google.common.collect.Maps;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.table.core.CMap;
import com.google.typography.font.sfntly.table.core.CMapTable;
import com.google.typography.font.sfntly.table.core.MaximumProfileTable;

import java.io.ByteArrayOutputStream;
import java.util.List;
import java.util.Map;

/**
 * Experimental CMap encoder, based primarily on writing the _inverse_ encoding.
 *
 * @author raph@google.com (Raph Levien)
 */
public class CmapEncoder {

  public static byte[] encode(Font font) {
    int nGlyphs = font.<MaximumProfileTable>getTable(Tag.maxp).numGlyphs();
    CMapTable cmapTable = font.getTable(Tag.cmap);
    CMap cmap = getBestCMap(cmapTable);
    Map<Integer, Integer> invEncoding = Maps.newHashMap();
    List<Integer> exceptions = Lists.newArrayList();
    for (Integer i : cmap) {
      int glyphId = cmap.glyphId(i);
      if (invEncoding.containsKey(glyphId)) {
        exceptions.add(i);
        exceptions.add(glyphId);
      } else {
        invEncoding.put(glyphId, i);
      }
    }
    ByteArrayOutputStream os = new ByteArrayOutputStream();
    int last = -1;
    for (int i = 0; i < nGlyphs; i++) {
      if (invEncoding.containsKey(i)) {
        int value = invEncoding.get(i);
        int delta = value - last;
        writeVShort(os, delta);
        last = value;
      } else {
        writeVShort(os, 0);
      }
    }
    for (int i : exceptions) {
      writeVShort(os, i);
    }
    return os.toByteArray();
  }

  private static CMap getBestCMap(CMapTable cmapTable) {
    for (CMap cmap : cmapTable) {
      if (cmap.format() == CMap.CMapFormat.Format12.value()) {
        return cmap;
      }
    }
    for (CMap cmap : cmapTable) {
      if (cmap.format() == CMap.CMapFormat.Format4.value()) {
        return cmap;
      }
    }
    return null;
  }

  // A simple signed varint encoding
  static void writeVShort(ByteArrayOutputStream os, int value) {
    if (value >= 0x2000 || value < -0x2000) {
      os.write((byte)(0x80 | ((value >> 14) & 0x7f)));
    }
    if (value >= 0x40 || value < -0x40) {
      os.write((byte)(0x80 | ((value >> 7) & 0x7f)));
    }
    os.write((byte)(value & 0x7f));
  }
}
