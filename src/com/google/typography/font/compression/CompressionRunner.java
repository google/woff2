// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Lists;
import com.google.common.collect.Sets;
import com.google.common.io.Files;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.FontFactory;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.data.ReadableFontData;
import com.google.typography.font.tools.conversion.eot.EOTWriter;
import com.google.typography.font.tools.conversion.eot.HdmxEncoder;
import com.google.typography.font.tools.conversion.woff.WoffWriter;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.List;
import java.util.Map.Entry;
import java.util.Set;

/**
 * A command-line tool for running different experimental compression code over
 * a corpus of fonts, and gathering statistics, particularly compression
 * efficiency.
 *
 * This is not intended to be production code, and for certain codecs it will
 * shell out to helper binaries, which is fine for the purposes of gathering
 * statistics, but obviously not much else.
 *
 * @author raph@google.com (Raph Levien)
 */
public class CompressionRunner {
  // private static final boolean DEBUG = false;

  public static void main(String[] args) throws IOException {
    boolean generateOutput = false;
    List<String> descs = Lists.newArrayList();
    String baseline = "gzip";

    List<String> filenames = Lists.newArrayList();
    for (int i = 0; i < args.length; i++) {
      if (args[i].charAt(0) == '-') {
        if (args[i].equals("-o")) {
          generateOutput = true;
        } else if (args[i].equals("-x")) {
          descs.add(args[i + 1]);
          i++;
        } else if (args[i].equals("-b")) {
          baseline = args[i + 1];
          i++;
        }
      } else {
        filenames.add(args[i]);
      }
    }

    // String baseline = "glyf/triplet,code,push:lzma";
    // String baseline = "glyf/cbbox,triplet,code,push:hdmx:lzma";
    // descs.add("woff2");
    if (descs.isEmpty()) {
      descs.add("glyf/cbbox,triplet,code,reslice:woff2");
    }
    runTest(filenames, baseline, descs, generateOutput);
  }

  private static void runTest(List<String> filenames, String baseline, List<String> descs,
      boolean generateOutput) throws IOException {
    PrintWriter o = new PrintWriter(System.out);
    List<StatsCollector> stats = Lists.newArrayList();
    for (int i = 0; i < descs.size(); i++) {
      stats.add(new StatsCollector());
    }
    FontFactory fontFactory = FontFactory.getInstance();
    o.println("<html>");
    for (String filename : filenames) {
      byte[] bytes = Files.toByteArray(new File(filename));
      Font font = fontFactory.loadFonts(bytes)[0];
      byte[] baselineResult = runExperiment(font, baseline);
      o.printf("<!-- %s: baseline %d bytes", new File(filename).getName(), baselineResult.length);
      for (int i = 0; i < descs.size(); i++) {
        byte[] expResult = runExperiment(font, descs.get(i));
        if (generateOutput) {
          String newFilename = filename;
          if (newFilename.endsWith(".ttf")) {
            newFilename = newFilename.substring(0, newFilename.length() - 4);
          }
          newFilename += ".woff2";
          Files.write(expResult, new File(newFilename));
        }
        double percent = 100. * expResult.length / baselineResult.length;
        stats.get(i).addStat(percent);
        o.printf(", %c %.2f%%", 'A' + i, percent);
      }
      o.printf(" -->\n");
    }
    stats.get(0).chartHeader(o, descs.size());
    for (int i = 0; i < descs.size(); i++) {
      stats.get(i).chartData(o, i + 1);
    }
    stats.get(0).chartEnd(o);
    o.printf("<p>baseline: %s</p>\n", baseline);
    for (int i = 0; i < descs.size(); i++) {
      StatsCollector sc = stats.get(i);
      o.printf("<p>%c: %s: median %f, mean %f</p>\n",
          'A' + i, descs.get(i), sc.median(), sc.mean());
    }
    stats.get(0).chartFooter(o);
    o.close();
  }

  private static Font.Builder stripTags(Font srcFont, Set<Integer> removeTags) {
    FontFactory fontFactory = FontFactory.getInstance();
    Font.Builder fontBuilder = fontFactory.newFontBuilder();

    for (Integer tag : srcFont.tableMap().keySet()) {
      if (!removeTags.contains(tag)) {
        fontBuilder.newTableBuilder(tag, srcFont.getTable(tag).readFontData());
      }
    }
    return fontBuilder;
  }

  private static Font.Builder preprocessMtxGlyf(Font srcFont, String options) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.<Integer>of());
    GlyfEncoder glyfEncoder = new GlyfEncoder(options);
    glyfEncoder.encode(srcFont);
    addTableBytes(fontBuilder, Tag.intValue(new byte[] {'g', 'l', 'z', '1'}),
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

  private static Font.Builder preprocessHmtx(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.hmtx));
    addTableBytes(fontBuilder, Tag.intValue(new byte[] {'h', 'm', 't', 'z'}),
        toBytes(AdvWidth.encode(srcFont)));
    return fontBuilder;
  }

  private static Font.Builder preprocessHdmx(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.hdmx));
    if (srcFont.hasTable(Tag.hdmx)) {
      addTableBytes(fontBuilder, Tag.hdmx, toBytes(new HdmxEncoder().encode(srcFont)));
    }
    return fontBuilder;
  }

  private static Font.Builder preprocessCmap(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.cmap));
    addTableBytes(fontBuilder, Tag.intValue(new byte[] {'c', 'm', 'a', 'z'}),
        CmapEncoder.encode(srcFont));
    return fontBuilder;
  }

  private static Font.Builder preprocessKern(Font srcFont) {
    Font.Builder fontBuilder = stripTags(srcFont, ImmutableSet.of(Tag.kern));
    if (srcFont.hasTable(Tag.kern)) {
      addTableBytes(fontBuilder, Tag.intValue(new byte[] {'k', 'e', 'r', 'z'}),
          toBytes(KernEncoder.encode(srcFont)));
    }
    return fontBuilder;
  }

  private static void addTableBytes(Font.Builder fontBuilder, int tag, byte[] contents) {
    fontBuilder.newTableBuilder(tag, ReadableFontData.createReadableFontData(contents));
  }

  private static byte[] fontToBytes(FontFactory fontFactory, Font font) {
    try {
      ByteArrayOutputStream baos = new ByteArrayOutputStream();
      fontFactory.serializeFont(font, baos);
      return baos.toByteArray();
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private static byte[] toBytes(ReadableFontData rfd) {
    byte[] result = new byte[rfd.length()];
    rfd.readBytes(0, result, 0, rfd.length());
    return result;
  }

  // This is currently implemented by shelling out to a command line helper,
  // which is fine for research purposes, but obviously problematic for
  // production.
  private static byte[] compressBzip2(byte[] input) {
    try {
      String[] args = {"/bin/bzip2"};
      CommandResult result = new Command(args).execute(input);
      return result.getStdout();
    } catch (CommandException e) {
      throw new RuntimeException(e);
    }
  }

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
  private static byte[] runExperiment(Font srcFont, String desc) throws IOException {
    Font font = srcFont;
    FontFactory fontFactory = FontFactory.getInstance();
    String[] pieces = desc.split(":");
    boolean keepDsig = false;
    for (int i = 0; i < pieces.length - 1; i++) {
      String[] piece = pieces[i].split("/");
      String cmd = piece[0];
      if (cmd.equals("glyf")) {
        font = preprocessMtxGlyf(font, piece.length > 1 ? piece[1] : "").build();
      } else if (cmd.equals("hmtx")) {
        font = preprocessHmtx(font).build();
      } else if (cmd.equals("hdmx")) {
        font = preprocessHdmx(font).build();
      } else if (cmd.equals("cmap")) {
        font = preprocessCmap(font).build();
      } else if (cmd.equals("kern")) {
        font = preprocessKern(font).build();
      } else if (cmd.equals("keepdsig")) {
        keepDsig = true;
      } else if (cmd.equals("strip")) {
        Set<Integer> removeTags = Sets.newTreeSet();
        for (String tag : piece[1].split(",")) {
          removeTags.add(Tag.intValue(tag.getBytes()));
        }
        font = stripTags(font, removeTags).build();
      }
    }
    if (!keepDsig) {
      font = stripTags(font, ImmutableSet.of(Tag.DSIG)).build();
    }
    String last = pieces[pieces.length - 1];
    String[] lastPieces = last.split("/");
    String lastBase = lastPieces[0];
    String lastArgs = lastPieces.length > 1 ? lastPieces[1] : "";
    if (!lastBase.equals("woff2")) {
      Set<Integer> tagsToStrip = Sets.newHashSet();
      for (Entry<Integer, Integer> mapping : Woff2Writer.getTransformMap().entrySet()) {
        if (font.hasTable(mapping.getValue())) {
          tagsToStrip.add(mapping.getKey());
        }
      }
      font = stripTags(font, tagsToStrip).build();
    }
    byte[] result = null;
    if (lastBase.equals("gzip")) {
      result = GzipUtil.deflate(fontToBytes(fontFactory, font));
    } else if (lastBase.equals("lzma")) {
      result = CompressLzma.compress(fontToBytes(fontFactory, font));
    } else if (lastBase.equals("bzip2")) {
      result = compressBzip2(fontToBytes(fontFactory, font));
    } else if (lastBase.equals("woff")) {
      result = toBytes(new WoffWriter().convert(font));
    } else if (lastBase.equals("woff2")) {
      result = toBytes(new Woff2Writer(lastArgs).convert(srcFont, font));
    } else if (lastBase.equals("eot")) {
      result = toBytes(new EOTWriter(true).convert(font));
    } else if (lastBase.equals("uncomp")) {
      result = fontToBytes(fontFactory, font);
    }
    return result;
  }
}
