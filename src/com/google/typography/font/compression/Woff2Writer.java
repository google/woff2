// Copyright 2012 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.common.collect.ImmutableBiMap;
import com.google.common.collect.Lists;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.data.WritableFontData;
import com.google.typography.font.sfntly.table.Table;
import com.google.typography.font.sfntly.table.core.FontHeaderTable;

import java.util.List;
import java.util.TreeSet;

public class Woff2Writer {
  private static final long SIGNATURE = 0x774f4632;
  private static final int WOFF2_HEADER_SIZE = 44;
  private static final int TABLE_ENTRY_SIZE = 5 * 4;
  private static final int FLAG_CONTINUE_STREAM = 1 << 4;
  private static final int FLAG_APPLY_TRANSFORM = 1 << 5;

  private final CompressionType compressionType;
  private final boolean longForm;

  Woff2Writer(String args) {
    CompressionType compressionType = CompressionType.NONE;
    boolean longForm = false;
    for (String arg : args.split(",")) {
      if ("lzma".equals(arg)) {
        compressionType = CompressionType.LZMA;
      } else if ("gzip".equals(arg)) {
        compressionType = CompressionType.GZIP;
      } else if ("short".equals(arg)) {
        longForm = false;
      } else if ("long".equals(arg)) {
        longForm = true;
      }
    }
    this.compressionType = compressionType;
    this.longForm = longForm;
  }

  private static ImmutableBiMap<Integer, Integer> TRANSFORM_MAP = ImmutableBiMap.of(
      Tag.glyf, Tag.intValue(new byte[] {'g', 'l', 'z', '1'}),
      Tag.loca, Tag.intValue(new byte[] {'l', 'o', 'c', 'z'})
  );

  public static ImmutableBiMap<Integer, Integer> getTransformMap() {
    return TRANSFORM_MAP;
  }

  private static ImmutableBiMap<Integer, Integer> KNOWN_TABLES =
      new ImmutableBiMap.Builder<Integer, Integer>()
      .put(Tag.intValue(new byte[] {'c', 'm', 'a', 'p'}), 0)
      .put(Tag.intValue(new byte[] {'h', 'e', 'a', 'd'}), 1)
      .put(Tag.intValue(new byte[] {'h', 'h', 'e', 'a'}), 2)
      .put(Tag.intValue(new byte[] {'h', 'm', 't', 'x'}), 3)
      .put(Tag.intValue(new byte[] {'m', 'a', 'x', 'p'}), 4)
      .put(Tag.intValue(new byte[] {'n', 'a', 'm', 'e'}), 5)
      .put(Tag.intValue(new byte[] {'O', 'S', '/', '2'}), 6)
      .put(Tag.intValue(new byte[] {'p', 'o', 's', 't'}), 7)
      .put(Tag.intValue(new byte[] {'c', 'v', 't', ' '}), 8)
      .put(Tag.intValue(new byte[] {'f', 'p', 'g', 'm'}), 9)
      .put(Tag.intValue(new byte[] {'g', 'l', 'y', 'f'}), 10)
      .put(Tag.intValue(new byte[] {'l', 'o', 'c', 'a'}), 11)
      .put(Tag.intValue(new byte[] {'p', 'r', 'e', 'p'}), 12)
      .put(Tag.intValue(new byte[] {'C', 'F', 'F', ' '}), 13)
      .put(Tag.intValue(new byte[] {'V', 'O', 'R', 'G'}), 14)
      .put(Tag.intValue(new byte[] {'E', 'B', 'D', 'T'}), 15)
      .put(Tag.intValue(new byte[] {'E', 'B', 'L', 'C'}), 16)
      .put(Tag.intValue(new byte[] {'g', 'a', 's', 'p'}), 17)
      .put(Tag.intValue(new byte[] {'h', 'd', 'm', 'x'}), 18)
      .put(Tag.intValue(new byte[] {'k', 'e', 'r', 'n'}), 19)
      .put(Tag.intValue(new byte[] {'L', 'T', 'S', 'H'}), 20)
      .put(Tag.intValue(new byte[] {'P', 'C', 'L', 'T'}), 21)
      .put(Tag.intValue(new byte[] {'V', 'D', 'M', 'X'}), 22)
      .put(Tag.intValue(new byte[] {'v', 'h', 'e', 'a'}), 23)
      .put(Tag.intValue(new byte[] {'v', 'm', 't', 'x'}), 24)
      .put(Tag.intValue(new byte[] {'B', 'A', 'S', 'E'}), 25)
      .put(Tag.intValue(new byte[] {'G', 'D', 'E', 'F'}), 26)
      .put(Tag.intValue(new byte[] {'G', 'P', 'O', 'S'}), 27)
      .put(Tag.intValue(new byte[] {'G', 'S', 'U', 'B'}), 28)
      .build();

  public WritableFontData convert(Font srcFont, Font font) {
    List<TableDirectoryEntry> entries = createTableDirectoryEntries(font);
    int size = computeCompressedFontSize(entries);
    WritableFontData writableFontData = WritableFontData.createWritableFontData(size);
    int index = 0;
    FontHeaderTable head = font.getTable(Tag.head);
    index += writeWoff2Header(writableFontData, entries, font.sfntVersion(), size,
        head.fontRevision());
    System.out.printf("Wrote header, index = %d\n", index);
    index += writeDirectory(writableFontData, index, entries);
    System.out.printf("Wrote directory, index = %d\n", index);
    index += writeTables(writableFontData, index, entries);
    System.out.printf("Wrote tables, index = %d\n", index);
    return writableFontData;
  }

  private List<TableDirectoryEntry> createTableDirectoryEntries(Font font) {
    List<TableDirectoryEntry> entries = Lists.newArrayList();
    TreeSet<Integer> tags = new TreeSet<Integer>(font.tableMap().keySet());

    for (int tag : tags) {
      Table table = font.getTable(tag);
      byte[] uncompressedBytes = bytesFromTable(table);
      byte[] transformedBytes = null;
      if (TRANSFORM_MAP.containsValue(tag)) {
        // Don't store the intermediate transformed tables under the nonstandard tags.
        continue;
      }
      if (TRANSFORM_MAP.containsKey(tag)) {
        int transformedTag = TRANSFORM_MAP.get(tag);
        Table transformedTable = font.getTable(transformedTag);
        if (transformedTable != null) {
          transformedBytes = bytesFromTable(transformedTable);
        }
      }
      if (transformedBytes == null) {
        entries.add(new TableDirectoryEntry(tag, uncompressedBytes, compressionType));
      } else {
        entries.add(new TableDirectoryEntry(tag, uncompressedBytes, transformedBytes,
            FLAG_APPLY_TRANSFORM, compressionType));
      }
    }
    return entries;
  }

  private byte[] bytesFromTable(Table table) {
    int length = table.dataLength();
    byte[] bytes = new byte[length];
    table.readFontData().readBytes(0, bytes, 0, length);
    return bytes;
  }

  private int writeWoff2Header(WritableFontData writableFontData,
      List<TableDirectoryEntry> entries,
      int flavor,
      int length,
      int version) {
    int index = 0;
    index += writableFontData.writeULong(index, SIGNATURE);
    index += writableFontData.writeULong(index, flavor);
    index += writableFontData.writeULong(index, length);
    index += writableFontData.writeUShort(index, entries.size());  // numTables
    index += writableFontData.writeUShort(index, 0);  // reserved
    int uncompressedFontSize = computeUncompressedSize(entries);
    index += writableFontData.writeULong(index, uncompressedFontSize);
    index += writableFontData.writeFixed(index, version);
    index += writableFontData.writeULong(index, 0);  // metaOffset
    index += writableFontData.writeULong(index, 0);  // metaLength
    index += writableFontData.writeULong(index, 0);  // metaOrigLength
    index += writableFontData.writeULong(index, 0);  // privOffset
    index += writableFontData.writeULong(index, 0);  // privLength
    return index;
  }

  private int writeDirectory(WritableFontData writableFontData, int offset,
      List<TableDirectoryEntry> entries) {
    int directorySize = computeDirectoryLength(entries);
    for (TableDirectoryEntry entry : entries) {
      offset += entry.writeEntry(writableFontData, offset);
    }
    return directorySize;
  }

  private int writeTables(WritableFontData writableFontData, int offset,
      List<TableDirectoryEntry> entries) {
    int start = offset;
    for (TableDirectoryEntry entry : entries) {
      offset += entry.writeData(writableFontData, offset);
      offset = align4(offset);
    }
    return offset - start;
  }

  private int computeDirectoryLength(List<TableDirectoryEntry> entries) {
    if (longForm) {
      return TABLE_ENTRY_SIZE * entries.size();
    } else {
      int size = 0;
      for (TableDirectoryEntry entry : entries) {
        size += entry.writeEntry(null, size);
      }
      return size;
    }
  }

  private int align4(int value) {
    return (value + 3) & -4;
  }

  private int computeUncompressedSize(List<TableDirectoryEntry> entries) {
    int size = 20 + 16 * entries.size();  // sfnt header length
    for (TableDirectoryEntry entry : entries) {
      size += entry.getOrigLength();
      size = align4(size);
    }
    return size;
  }

  private int computeCompressedFontSize(List<TableDirectoryEntry> entries) {
    int fontSize = WOFF2_HEADER_SIZE;
    fontSize += computeDirectoryLength(entries);
    for (TableDirectoryEntry entry : entries) {
      fontSize += entry.getCompLength();
      fontSize = align4(fontSize);
    }
    return fontSize;
  }

  private enum CompressionType {
    NONE, GZIP, LZMA
  }

  private static long flagsForCompression(CompressionType compressionType) {
    switch (compressionType) {
      case NONE:
        return 0;
      case GZIP:
        return 1;
      case LZMA:
        return 2;
    }
    return 0;
  }

  private static byte[] compress(byte[] input, CompressionType compressionType) {
    switch (compressionType) {
      case NONE:
        return input;
      case GZIP:
        return GzipUtil.deflate(input);
      case LZMA:
        return CompressLzma.compress(input);
    }
    return null;
  }

  // Note: if writableFontData is null, just return the size
  private static int writeBase128(WritableFontData writableFontData, long value, int offset) {
    int size = 1;
    long tmpValue = value;
    while (tmpValue >= 128) {
      size += 1;
      tmpValue = tmpValue >> 7;
    }
    for (int i = 0; i < size; i++) {
      int b = (int)(value >> (7 * (size - i - 1))) & 0x7f;
      if (i < size - 1) {
        b |= 0x80;
      }
      if (writableFontData != null) {
        writableFontData.writeByte(offset, (byte)b);
      }
      offset += 1;
    }
    return size;
  }

  private class TableDirectoryEntry {
    private final long tag;
    private final long flags;
    private final long origLength;
    private final long transformLength;
    private final byte[] bytes;

    // This is the constructor for tables that don't have transforms
    public TableDirectoryEntry(long tag, byte[] uncompressedBytes,
        CompressionType compressionType) {
      this(tag, uncompressedBytes, uncompressedBytes, 0, compressionType);
    }

    public TableDirectoryEntry(long tag, byte[] uncompressedBytes, byte[] transformedBytes,
        long transformFlags, CompressionType compressionType) {
      byte[] compressedBytes = compress(transformedBytes, compressionType);
      if (compressedBytes.length >= transformedBytes.length) {
        compressedBytes = transformedBytes;
        compressionType = CompressionType.NONE;
      }
      this.tag = tag;
      this.flags = transformFlags | flagsForCompression(compressionType);
      this.origLength = uncompressedBytes.length;
      this.transformLength = transformedBytes.length;
      this.bytes = compressedBytes;
    }

    public long getOrigLength() {
      return origLength;
    }

    public long getCompLength() {
      return bytes.length;
    }

    // Note: if writableFontData is null, just return the size
    public int writeEntry(WritableFontData writableFontData, int offset) {
      if (longForm) {
        if (writableFontData != null) {
          offset += writableFontData.writeULong(offset, tag);
          offset += writableFontData.writeULong(offset, flags);
          offset += writableFontData.writeULong(offset, getCompLength());
          offset += writableFontData.writeULong(offset, transformLength);
          offset += writableFontData.writeULong(offset, getOrigLength());
        }
        return TABLE_ENTRY_SIZE;
      } else {
        int start = offset;
        int flag_byte = 0x1f;
        if (KNOWN_TABLES.containsKey((int)tag)) {
          flag_byte = KNOWN_TABLES.get((int)tag);
        }
        if ((flags & FLAG_APPLY_TRANSFORM) != 0) {
          flag_byte |= 0x20;
        }
        if ((flags & FLAG_CONTINUE_STREAM) != 0) {
          flag_byte |= 0xc0;
        } else {
          flag_byte |= (flags & 3) << 6;
        }
        if (writableFontData != null) {
          System.out.printf("%d: tag = %08x, flag = %02x\n", offset, tag, flag_byte);
          writableFontData.writeByte(offset, (byte)flag_byte);
        }
        offset += 1;
        if ((flag_byte & 0x1f) == 0x1f) {
          if (writableFontData != null) {
            writableFontData.writeULong(offset, tag);
          }
          offset += 4;
        }
        offset += writeBase128(writableFontData, getOrigLength(), offset);
        if ((flag_byte & 0x20) != 0) {
          offset += writeBase128(writableFontData, transformLength, offset);
        }
        if ((flag_byte & 0xc0) == 0x40 || (flag_byte & 0xc0) == 0x80) {
          offset += writeBase128(writableFontData, getCompLength(), offset);
        }
        return offset - start;
      }
    }

    public int writeData(WritableFontData writableFontData, int offset) {
      writableFontData.writeBytes(offset, bytes);
      return bytes.length;
    }
  }
}
