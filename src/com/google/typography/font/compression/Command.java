// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.common.io.ByteStreams;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * This is a simple wrapper to run a commandline as a pipe. It's not
 * particularly efficient, robust, or featureful.
 */
public class Command {

  private class StreamConsumer extends Thread {
    private final InputStream is;
    private byte[] result = null;

    public StreamConsumer(InputStream is) {
      this.is = is;
    }

    @Override
    public void run() {
      try {
        result = ByteStreams.toByteArray(is);
      } catch (IOException e) {
        // TODO: handle this well
      }
    }

    public byte[] getResult() {
      return result;
    }
  }

  private final ProcessBuilder processBuilder;

  public Command(String[] args) {
    processBuilder = new ProcessBuilder(args);
  }

  public CommandResult execute(byte[] input) throws CommandException {
    Process process;
    try {
      process = processBuilder.start();
    } catch (IOException e) {
      throw new CommandException("exec failed");
    }
    StreamConsumer sc = new StreamConsumer(process.getInputStream());
    sc.start();
    OutputStream stdin = process.getOutputStream();
    try {
      stdin.write(input);
      stdin.close();
    } catch (IOException e) {
      throw new CommandException("error writing input");
    }
    try {
      process.waitFor();
      sc.join();
    } catch (InterruptedException e) {
      throw new CommandException("interrupted");
    }
    return new CommandResult(sc.getResult());
  }
}

