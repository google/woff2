// Copyright 2012 Google Inc. All Rights Reserved.

package com.google.typography.font.compression;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;

import SevenZip.Compression.LZMA.Encoder;

/**
 * @author raph@google.com (Raph Levien)
 */
public class CompressLzma {

  public static byte[] compress(byte[] input) {
    try {
      ByteArrayInputStream in = new ByteArrayInputStream(input);
      ByteArrayOutputStream out = new ByteArrayOutputStream();

      Encoder encoder = new Encoder();
      encoder.SetAlgorithm(2);
      encoder.SetDictionarySize(1 << 23);
      encoder.SetNumFastBytes(128);
      encoder.SetMatchFinder(1);
      encoder.SetLcLpPb(3, 0, 2);
      encoder.SetEndMarkerMode(true);
      encoder.WriteCoderProperties(out);
      for (int i = 0; i < 8; i++) {
        out.write((int) ((long) -1 >>> (8 * i)) & 0xFF);
      }
      encoder.Code(in, out, -1, -1, null);

      out.flush();
      out.close();

      return out.toByteArray();
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
