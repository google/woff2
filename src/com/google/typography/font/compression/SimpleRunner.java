package com.google.typography.font.compression;

import com.google.common.io.Files;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.FontFactory;

import java.io.File;
import java.io.IOException;

/**
 * Simple WOFF 2.0 compression report runner.
 *
 * @author David Kuettel
 */
public class SimpleRunner {

  private static final FontFactory FONT_FACTORY = FontFactory.getInstance();

  private static final String GZIP = "gzip";
  private static final String WOFF2 = "glyf/cbbox,triplet,code,reslice:woff2/lzma";

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
      File file = new File(filename);
      byte[] bytes = Files.toByteArray(file);
      Font font = FONT_FACTORY.loadFonts(bytes)[0];

      byte[] gzip = Experiment.run(font, GZIP);
      byte[] woff2 = Experiment.run(font, WOFF2);

      stats.add(
          CompressionStats.Stats.builder()
              .setFilename(file.getName())
              .setSize(CompressionStats.Size.ORIGINAL, bytes.length)
              .setSize(CompressionStats.Size.GZIP, gzip.length)
              .setSize(CompressionStats.Size.WOFF2, woff2.length)
              .build());
    }
  }

  private static void usage() {
    System.err.println("Usage: SimpleRunner font...");
    System.exit(-1);
  }
}
