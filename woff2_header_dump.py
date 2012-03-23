# Copyright (c) 2012 Google Inc. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This is a simple utility for dumping out the header of a compressed file, and
# is suitable for doing spot checks of compressed. files. However, this only
# implements the "long" form of the table directory.

import struct
import sys

def dump_woff2_header(header):
  header_values = struct.unpack('>IIIHHIHHIIIII', header[:44])
  for i, key in enumerate([
    'signature',
    'flavor',
    'length',
    'numTables',
    'reserved',
    'totalSfntSize',
    'majorVersion',
    'minorVersion',
    'metaOffset',
    'metaOrigLength',
    'privOffset',
    'privLength']):
    print key, header_values[i]
  numTables = header_values[3]
  for i in range(numTables):
    entry = struct.unpack('>IIIII', header[44+20*i:44+20*(i+1)])
    print '%08x %d %d %d %d' % entry

def main():
  header = file(sys.argv[1]).read()
  dump_woff2_header(header)

main()

