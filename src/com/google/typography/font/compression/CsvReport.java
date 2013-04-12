package com.google.typography.font.compression;

import com.google.common.io.Closeables;

import java.io.BufferedWriter;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;

/**
 * Generates a CSV report containing the compression stats.
 *
 * @author David Kuettel
 */
public class CsvReport {

  public static void create(CompressionStats stats, String filename) throws IOException {
    PrintWriter writer = new PrintWriter(new BufferedWriter(new FileWriter(filename)));
    try {
      writer.printf("Font, Original, GZIP, WOFF 2.0\n");
      for (CompressionStats.Stats stat : stats.values()) {
        writer.printf("%s, %d, %d, %d\n",
            stat.getFilename(),
            stat.getSize(CompressionStats.Size.ORIGINAL),
            stat.getSize(CompressionStats.Size.GZIP),
            stat.getSize(CompressionStats.Size.WOFF2));
      }
    } finally {
      Closeables.closeQuietly(writer);
    }
  }
}
