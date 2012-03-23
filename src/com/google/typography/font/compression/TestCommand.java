// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import java.io.IOException;

/**
 * A simple test for the command mechanism. Quick and dirty run with:
 * java -cp 'build/classes:lib/guava-11.0.1.jar' com/google/typography/font/compression/TestCommand
 */
public class TestCommand {
  public static void main(String[] args) throws IOException {
    String[] commandArgs = {"/usr/bin/lzma"};
    byte[] input = new byte[16384];
    try {
      CommandResult result = new Command(commandArgs).execute(input);
      byte[] output = result.getStdout();
      for (int i = 0; i < output.length; i++) {
        System.out.printf("%02x\n", output[i] & 0xff);
      }
    } catch (CommandException e) {
      e.printStackTrace();
    };
  }
}

