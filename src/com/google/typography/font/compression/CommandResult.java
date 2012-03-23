// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

public final class CommandResult {
  private final byte[] stdout;

  public CommandResult(byte[] stdout) {
    this.stdout = stdout;
  }

  public byte[] getStdout() {
    return stdout;
  }
}

