// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A very simple commandline tool for decompressing woff2 format files
// (given as argc[1]), writing the decompressed version to stdout.

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "opentype-sanitiser.h"
#include "woff2.h"

namespace {

int Usage(const char *argv0) {
  std::fprintf(stderr, "Usage: %s woff2_file > dest_ttf_file\n", argv0);
  return 1;
}

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) return Usage(argv[0]);
  if (::isatty(1)) return Usage(argv[0]);

  const int fd = ::open(argv[1], O_RDONLY);
  if (fd < 0) {
    ::perror("open");
    return 1;
  }

  struct stat st;
  ::fstat(fd, &st);

  uint8_t *data = new uint8_t[st.st_size];
  if (::read(fd, data, st.st_size) != st.st_size) {
    ::perror("read");
    return 1;
  }
  ::close(fd);

  size_t decompressed_size = ots::ComputeWOFF2FinalSize(data, st.st_size);
  if (decompressed_size == 0) {
    std::fprintf(stderr, "Error computing decompressed file size!\n");
    return 1;
  }
  uint8_t *buf = new uint8_t[decompressed_size];
  const bool result = ots::ConvertWOFF2ToTTF(buf, decompressed_size,
    data, st.st_size);

  if (!result) {
    std::fprintf(stderr, "Failed to decompress file!\n");
  }
  fwrite(buf, 1, decompressed_size, stdout);
  return !result;
}
