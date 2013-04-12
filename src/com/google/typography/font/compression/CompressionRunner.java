// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.common.collect.Lists;
import com.google.common.io.Files;
import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.FontFactory;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.util.List;

/**
 * A command-line tool for running different experimental compression code over
 * a corpus of fonts, and gathering statistics, particularly compression
 * efficiency.
 *
 * This is not intended to be production code.
 *
 * @author Raph Levien
 */
public class CompressionRunner {

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
      descs.add("glyf/cbbox,triplet,code,reslice:woff2/lzma");
    }
    run(filenames, baseline, descs, generateOutput);
  }

  private static void run(List<String> filenames, String baseline, List<String> descs,
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
      byte[] baselineResult = Experiment.run(font, baseline);
      o.printf("<!-- %s: baseline %d bytes", new File(filename).getName(), baselineResult.length);
      for (int i = 0; i < descs.size(); i++) {
        byte[] expResult = Experiment.run(font, descs.get(i));
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
}
