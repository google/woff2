package com.google.typography.font.compression;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.FontFactory;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.tools.conversion.eot.EOTWriter;
import com.google.typography.font.tools.conversion.woff.WoffWriter;

import java.io.IOException;
import java.util.Map;
import java.util.Set;

/**
 * @author Raph Levien
 */
public class Experiment {

  /**
   * Does one experimental compression on a font, using the string to guide what
   * gets done.
   *
   * @param srcFont Source font
   * @param desc experiment description string; the exact format is probably
   *        still evolving
   * @return serialization of compressed font
   * @throws IOException
   */
  public static byte[] run(Font srcFont, String desc) throws IOException {
    Font font = srcFont;
    FontFactory fontFactory = FontFactory.getInstance();
    String[] pieces = desc.split(":");
    boolean keepDsig = false;

    for (int i = 0; i < pieces.length - 1; i++) {
      String[] piece = pieces[i].split("/");
      String cmd = piece[0];
      if (cmd.equals("glyf")) {
        font = FontUtil.preprocessMtxGlyf(font, piece.length > 1 ? piece[1] : "").build();
      } else if (cmd.equals("hmtx")) {
        font = FontUtil.preprocessHmtx(font).build();
      } else if (cmd.equals("hdmx")) {
        font = FontUtil.preprocessHdmx(font).build();
      } else if (cmd.equals("cmap")) {
        font = FontUtil.preprocessCmap(font).build();
      } else if (cmd.equals("kern")) {
        font = FontUtil.preprocessKern(font).build();
      } else if (cmd.equals("keepdsig")) {
        keepDsig = true;
      } else if (cmd.equals("strip")) {
        Set<Integer> removeTags = Sets.newTreeSet();
        for (String tag : piece[1].split(",")) {
          removeTags.add(Tag.intValue(tag.getBytes()));
        }
        font = FontUtil.stripTags(font, removeTags).build();
      }
    }
    if (!keepDsig) {
      font = FontUtil.stripTags(font, ImmutableSet.of(Tag.DSIG)).build();
    }

    String last = pieces[pieces.length - 1];
    String[] lastPieces = last.split("/");
    String lastBase = lastPieces[0];
    String lastArgs = lastPieces.length > 1 ? lastPieces[1] : "";
    if (!lastBase.equals("woff2")) {
      Set<Integer> tagsToStrip = Sets.newHashSet();
      for (Map.Entry<Integer, Integer> mapping : Woff2Writer.getTransformMap().entrySet()) {
        if (font.hasTable(mapping.getValue())) {
          tagsToStrip.add(mapping.getKey());
        }
      }
      font = FontUtil.stripTags(font, tagsToStrip).build();
    }

    byte[] result = null;
    if (lastBase.equals("gzip")) {
      result = GzipUtil.deflate(FontUtil.toBytes(fontFactory, font));
    } else if (lastBase.equals("lzma")) {
      result = CompressLzma.compress(FontUtil.toBytes(fontFactory, font));
    } else if (lastBase.equals("woff")) {
      result = FontUtil.toBytes(new WoffWriter().convert(font));
    } else if (lastBase.equals("woff2")) {
      result = FontUtil.toBytes(new Woff2Writer(lastArgs).convert(font));
    } else if (lastBase.equals("eot")) {
      result = FontUtil.toBytes(new EOTWriter(true).convert(font));
    } else if (lastBase.equals("uncomp")) {
      result = FontUtil.toBytes(fontFactory, font);
    }
    return result;
  }
}
