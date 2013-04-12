package com.google.typography.font.compression;

import com.google.common.collect.ImmutableSet;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.FontFactory;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.data.ReadableFontData;
import com.google.typography.font.tools.conversion.eot.HdmxEncoder;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;
import java.util.Set;

/**
 * Font utility methods
 *
 * @author Raph Levien
 */
public class FontUtil {

  public static Font.Builder stripTags(Font srcFont, Set<Integer> removeTags) {
    FontFactory fontFactory = FontFactory.getInstance();
    Font.Builder fontBuilder = fontFactory.newFontBuilder();

    for (Integer tag : srcFont.tableMap().keySet()) {
      if (!removeTags.contains(tag)) {
        fontBuilder.newTableBuilder(tag, srcFont.getTable(tag).readFontData());
      }
    }
    return fontBuilder;
  }

  public static Font.Builder preprocessMtxGlyf(Font srcFont, String options) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.<Integer>of());
    GlyfEncoder glyfEncoder = new GlyfEncoder(options);
    glyfEncoder.encode(srcFont);
    addTableBytes(fontBuilder, Tag.intValue(new byte[]{'g', 'l', 'z', '1'}),
        glyfEncoder.getGlyfBytes());
    addTableBytes(fontBuilder, Tag.intValue(new byte[] {'l', 'o', 'c', 'z'}),
        glyfEncoder.getLocaBytes());
    if (!Arrays.asList(options.split(",")).contains("reslice")) {
      addTableBytes(fontBuilder, Tag.intValue(new byte[] {'g', 'l', 'z', '2'}),
          glyfEncoder.getCodeBytes());
      addTableBytes(fontBuilder, Tag.intValue(new byte[] {'g', 'l', 'z', '3'}),
          glyfEncoder.getPushBytes());
    }
    return fontBuilder;
  }

  public static Font.Builder preprocessHmtx(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.hmtx));
    addTableBytes(fontBuilder, Tag.intValue(new byte[] {'h', 'm', 't', 'z'}),
        toBytes(AdvWidth.encode(srcFont)));
    return fontBuilder;
  }

  public static Font.Builder preprocessHdmx(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.hdmx));
    if (srcFont.hasTable(Tag.hdmx)) {
      addTableBytes(fontBuilder, Tag.hdmx, toBytes(new HdmxEncoder().encode(srcFont)));
    }
    return fontBuilder;
  }

  public static Font.Builder preprocessCmap(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.cmap));
    addTableBytes(fontBuilder, Tag.intValue(new byte[] {'c', 'm', 'a', 'z'}),
        CmapEncoder.encode(srcFont));
    return fontBuilder;
  }

  public static Font.Builder preprocessKern(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.kern));
    if (srcFont.hasTable(Tag.kern)) {
      addTableBytes(fontBuilder, Tag.intValue(new byte[] {'k', 'e', 'r', 'z'}),
          toBytes(KernEncoder.encode(srcFont)));
    }
    return fontBuilder;
  }

  public static void addTableBytes(Font.Builder fontBuilder, int tag, byte[] contents) {
    fontBuilder.newTableBuilder(tag, ReadableFontData.createReadableFontData(contents));
  }

  public static byte[] toBytes(FontFactory fontFactory, Font font) {
    try {
      ByteArrayOutputStream baos = new ByteArrayOutputStream();
      fontFactory.serializeFont(font, baos);
      return baos.toByteArray();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  public static byte[] toBytes(ReadableFontData rfd) {
    byte[] result = new byte[rfd.length()];
    rfd.readBytes(0, result, 0, rfd.length());
    return result;
  }
}
