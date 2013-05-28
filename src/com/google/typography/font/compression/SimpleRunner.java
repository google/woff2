package com.google.typography.font.compression;

import com.google.common.io.Files;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.FontFactory;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.table.truetype.GlyphTable;
import com.google.typography.font.sfntly.table.truetype.LocaTable;

import java.io.File;
import java.io.IOException;

/**
 * Simple WOFF 2.0 compression report runner.
 *
 * @author David Kuettel
 */
public class SimpleRunner {

  private static final FontFactory FONT_FACTORY = FontFactory.getInstance();

  private static final String WOFF = "woff";
  private static final String WOFF2 = "woff2/lzma";

  private static final String TRUETYPE = "glyf/cbbox,triplet,code,reslice";
  private static final String CFF = "";

  private static final String REPORT = "report.csv";

  public static void main(String[] args) throws IOException {
    if (args.length == 0) {
      usage();
    }
    CompressionStats stats = new CompressionStats();

    System.out.printf("Analyzing (%d) fonts\n", args.length);
    run(stats, args);

    System.out.printf("Creating report: %s\n", REPORT);
    CsvReport.create(stats, REPORT);
  }

  private static void run(CompressionStats stats, String[] filenames) throws IOException {
    for (String filename : filenames) {
      try {
        File file = new File(filename);
        byte[] bytes = Files.toByteArray(file);
        Font font = FONT_FACTORY.loadFonts(bytes)[0];

        byte[] woff = Experiment.run(font, WOFF);
        byte[] woff2 = Experiment.run(font, getOptions(font));

        CompressionStats.Stats stat = CompressionStats.Stats.builder()
            .setFilename(file.getName())
            .setSize(CompressionStats.Size.ORIGINAL, bytes.length)
            .setSize(CompressionStats.Size.WOFF, woff.length)
            .setSize(CompressionStats.Size.WOFF2, woff2.length)
            .build();
        stats.add(stat);

        System.out.printf("> %s, %d, %d, %d, %.2f%%\n",
            stat.getFilename(),
            stat.getSize(CompressionStats.Size.ORIGINAL),
            stat.getSize(CompressionStats.Size.WOFF),
            stat.getSize(CompressionStats.Size.WOFF2),
            stat.getPercent(CompressionStats.Size.WOFF, CompressionStats.Size.WOFF2));

      } catch (Throwable t) {
        System.err.printf("WARNING: failed to compress: %s\n", filename);
        t.printStackTrace();
      }
    }
  }

  private static boolean isTrueType(Font font) {
    LocaTable loca = font.getTable(Tag.loca);
    GlyphTable glyf = font.getTable(Tag.glyf);
    return (loca != null && glyf != null);
  }

  private static String getOptions(Font font) {
    return String.format("%s:%s", (isTrueType(font)) ? TRUETYPE : CFF, WOFF2);
  }

  private static void usage() {
    System.err.println("Usage: SimpleRunner font...");
    System.exit(-1);
  }
}
