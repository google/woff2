// Copyright 2012 Google Inc. All Rights Reserved.

package com.google.typography.font.compression;

/**
 * @author raph@google.com (Raph Levien)
 */
public class CompressLzma {
  // This is currently implemented by shelling out to a command line helper,
  // which is fine for research purposes, but obviously problematic for
  // production.
  public static byte[] compress(byte[] input) {
    try {
      String[] args = {"/usr/bin/lzma"};
      CommandResult result = new Command(args).execute(input);
      return result.getStdout();
    } catch (CommandException e) {
      throw new RuntimeException(e);
    }
  }

}
