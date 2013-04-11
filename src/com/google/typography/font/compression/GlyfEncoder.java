// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.data.ReadableFontData;
import com.google.typography.font.sfntly.table.core.FontHeaderTable;
import com.google.typography.font.sfntly.table.truetype.CompositeGlyph;
import com.google.typography.font.sfntly.table.truetype.Glyph;
import com.google.typography.font.sfntly.table.truetype.GlyphTable;
import com.google.typography.font.sfntly.table.truetype.LocaTable;
import com.google.typography.font.sfntly.table.truetype.SimpleGlyph;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Implementation of compression of CTF glyph data, as per sections 5.6-5.10 and 6 of the spec.
 * This is a hacked-up version with a number of options, for experimenting.
 *
 * @author Raph Levien
 */
public class GlyfEncoder {

  private final ByteArrayOutputStream nContourStream;
  private final ByteArrayOutputStream nPointsStream;
  private final ByteArrayOutputStream flagBytesStream;
  private final ByteArrayOutputStream compositeStream;
  private final ByteArrayOutputStream bboxStream;
  private final ByteArrayOutputStream glyfStream;
  private final ByteArrayOutputStream pushStream;
  private final ByteArrayOutputStream codeStream;
  private final boolean sbbox;
  private final boolean cbbox;
  private final boolean code;
  private final boolean triplet;
  private final boolean doPush;
  private final boolean doHop;
  private final boolean push2byte;
  private final boolean reslice;

  private int nGlyphs;
  private byte[] bboxBitmap;
  private FontHeaderTable.IndexToLocFormat indexFmt;

  public GlyfEncoder(String options) {
    glyfStream = new ByteArrayOutputStream();
    pushStream = new ByteArrayOutputStream();
    codeStream = new ByteArrayOutputStream();
    nContourStream = new ByteArrayOutputStream();
    nPointsStream = new ByteArrayOutputStream();
    flagBytesStream = new ByteArrayOutputStream();
    compositeStream = new ByteArrayOutputStream();
    bboxStream = new ByteArrayOutputStream();
    boolean sbbox = false;
    boolean cbbox = false;
    boolean code = false;
    boolean triplet = false;
    boolean doPush = false;
    boolean reslice = false;
    boolean doHop = false;
    boolean push2byte = false;
    for (String option : options.split(",")) {
      if (option.equals("sbbox")) {
        sbbox = true;
      } else if (option.equals("cbbox")) {
        cbbox = true;
      } else if (option.equals("code")) {
        code = true;
      } else if (option.equals("triplet")) {
        triplet = true;
      } else if (option.equals("push")) {
        doPush = true;
      } else if (option.equals("hop")) {
        doHop = true;
      } else if (option.equals("push2byte")) {
        push2byte = true;
      } else if (option.equals("reslice")) {
        reslice = true;
      }
    }
    this.sbbox = sbbox;
    this.cbbox = cbbox;
    this.code = code;
    this.triplet = triplet;
    this.doPush = doPush;
    this.doHop = doHop;
    this.push2byte = push2byte;
    this.reslice = reslice;
  }

  public void encode(Font sourceFont) {
    FontHeaderTable head = sourceFont.getTable(Tag.head);
    indexFmt = head.indexToLocFormat();
    LocaTable loca = sourceFont.getTable(Tag.loca);
    nGlyphs = loca.numGlyphs();
    GlyphTable glyf = sourceFont.getTable(Tag.glyf);
    bboxBitmap = new byte[((nGlyphs + 31) >> 5) << 2];

    for (int glyphId = 0; glyphId < nGlyphs; glyphId++) {
      int sourceOffset = loca.glyphOffset(glyphId);
      int length = loca.glyphLength(glyphId);
      Glyph glyph = glyf.glyph(sourceOffset, length);
      writeGlyph(glyphId, glyph);
    }
  }

  private void writeGlyph(int glyphId, Glyph glyph) {
    try {
      if (glyph == null || glyph.dataLength() == 0) {
        writeNContours(0);
      } else if (glyph instanceof SimpleGlyph) {
        writeSimpleGlyph(glyphId, (SimpleGlyph)glyph);
      } else if (glyph instanceof CompositeGlyph) {
        writeCompositeGlyph(glyphId, (CompositeGlyph)glyph);
      }
    } catch (IOException e) {
      throw new RuntimeException("unexpected IOException writing glyph data", e);
    }
  }

  private void writeInstructions(Glyph glyph) throws IOException{
    if (doPush) {
      splitPush(glyph);
    } else {
      int pushCount = 0;
      int codeSize = glyph.instructionSize();
      if (!reslice) {
        write255UShort(glyfStream, pushCount);
      }
      write255UShort(glyfStream, codeSize);
      if (codeSize > 0) {
        if (code) {
          glyph.instructions().copyTo(codeStream);
        } else {
          glyph.instructions().copyTo(glyfStream);
        }
      }
    }
  }

  private void writeSimpleGlyph(int glyphId, SimpleGlyph glyph) throws IOException {
    int numContours = glyph.numberOfContours();
    writeNContours(numContours);
    if (sbbox) {
      writeBbox(glyphId, glyph);
    }
    // TODO: check that bbox matches, write bbox if not
    for (int i = 0; i < numContours; i++) {
      if (reslice) {
        write255UShort(nPointsStream, glyph.numberOfPoints(i));
      } else {
        write255UShort(glyfStream, glyph.numberOfPoints(i) - (i == 0 ? 1 : 0));
      }
    }
    ByteArrayOutputStream os = new ByteArrayOutputStream();
    int lastX = 0;
    int lastY = 0;
    for (int i = 0; i < numContours; i++) {
      int numPoints = glyph.numberOfPoints(i);
      for (int j = 0; j < numPoints; j++) {
        int x = glyph.xCoordinate(i, j);
        int y = glyph.yCoordinate(i, j);
        int dx = x - lastX;
        int dy = y - lastY;
        if (triplet) {
          writeTriplet(os, glyph.onCurve(i, j), dx, dy);
        } else {
          writeVShort(os, dx * 2 + (glyph.onCurve(i, j) ? 1 : 0));
          writeVShort(os, dy);
        }
        lastX = x;
        lastY = y;
      }
    }
    os.writeTo(glyfStream);
    if (numContours > 0) {
      writeInstructions(glyph);
    }
  }

  private void writeCompositeGlyph(int glyphId, CompositeGlyph glyph) throws IOException {
    boolean haveInstructions = false;
    writeNContours(-1);
    if (cbbox) {
      writeBbox(glyphId, glyph);
    }
    ByteArrayOutputStream outStream = reslice ? compositeStream : glyfStream;
    for (int i = 0; i < glyph.numGlyphs(); i++) {
      int flags = glyph.flags(i);
      writeUShort(outStream, flags);
      haveInstructions = (flags & CompositeGlyph.FLAG_WE_HAVE_INSTRUCTIONS) != 0;
      writeUShort(outStream, glyph.glyphIndex(i));
      if ((flags & CompositeGlyph.FLAG_ARG_1_AND_2_ARE_WORDS) == 0) {
        outStream.write(glyph.argument1(i));
        outStream.write(glyph.argument2(i));
      } else {
        writeUShort(outStream, glyph.argument1(i));
        writeUShort(outStream, glyph.argument2(i));
      }
      if (glyph.transformationSize(i) != 0) {
        try {
          outStream.write(glyph.transformation(i));
        } catch (IOException e) {
        }
      }
    }
    if (haveInstructions) {
      writeInstructions(glyph);
    }
  }

  private void writeNContours(int nContours) {
    if (reslice) {
      writeUShort(nContourStream, nContours);
    } else {
      writeUShort(nContours);
    }
  }

  private void writeBbox(int glyphId, Glyph glyph) {
    if (reslice) {
      bboxBitmap[glyphId >> 3] |= 0x80 >> (glyphId & 7);
    }
    ByteArrayOutputStream outStream = reslice ? bboxStream : glyfStream;
    writeUShort(outStream, glyph.xMin());
    writeUShort(outStream, glyph.yMin());
    writeUShort(outStream, glyph.xMax());
    writeUShort(outStream, glyph.yMax());
  }

  private void writeUShort(ByteArrayOutputStream os, int value) {
    os.write(value >> 8);
    os.write(value & 255);
  }

  private void writeUShort(int value) {
    writeUShort(glyfStream, value);
  }

  private void writeLong(OutputStream os, int value) throws IOException {
    os.write((value >> 24) & 255);
    os.write((value >> 16) & 255);
    os.write((value >> 8) & 255);
    os.write(value & 255);
  }

  // As per 6.1.1 of spec
  // visible for testing
  static void write255UShort(ByteArrayOutputStream os, int value) {
    if (value < 0) {
      throw new IllegalArgumentException();
    }
    if (value < 253) {
      os.write((byte)value);
    } else if (value < 506) {
      os.write(255);
      os.write((byte)(value - 253));
    } else if (value < 762) {
      os.write(254);
      os.write((byte)(value - 506));
    } else {
      os.write(253);
      os.write((byte)(value >> 8));
      os.write((byte)(value & 0xff));
    }
  }

  // As per 6.1.1 of spec
  // visible for testing
  static void write255Short(OutputStream os, int value) throws IOException {
    int absValue = Math.abs(value);
    if (value < 0) {
      // spec is unclear about whether words should be signed. This code is conservative, but we
      // can test once the implementation is working.
      os.write(250);
    }
    if (absValue < 250) {
      os.write((byte)absValue);
    } else if (absValue < 500) {
      os.write(255);
      os.write((byte)(absValue - 250));
    } else if (absValue < 756) {
      os.write(254);
      os.write((byte)(absValue - 500));
    } else {
      os.write(253);
      os.write((byte)(absValue >> 8));
      os.write((byte)(absValue & 0xff));
    }
  }

  // A simple signed varint encoding
  static void writeVShort(ByteArrayOutputStream os, int value) {
    if (value >= 0x2000 || value < -0x2000) {
      os.write((byte)(0x80 | ((value >> 14) & 0x7f)));
    }
    if (value >= 0x40 || value < -0x40) {
      os.write((byte)(0x80 | ((value >> 7) & 0x7f)));
    }
    os.write((byte)(value & 0x7f));
  }

  // As in section 5.11 of the spec
  // visible for testing
  void writeTriplet(OutputStream os, boolean onCurve, int x, int y) throws IOException {
    int absX = Math.abs(x);
    int absY = Math.abs(y);
    int onCurveBit = onCurve ? 0 : 128;
    int xSignBit = (x < 0) ? 0 : 1;
    int ySignBit = (y < 0) ? 0 : 1;
    int xySignBits = xSignBit + 2 * ySignBit;
    ByteArrayOutputStream flagStream = reslice ? flagBytesStream : glyfStream;

    if (x == 0 && absY < 1280) {
      flagStream.write(onCurveBit + ((absY & 0xf00) >> 7) + ySignBit);
      os.write(absY & 0xff);
    } else if (y == 0 && absX < 1280) {
      flagStream.write(onCurveBit + 10 + ((absX & 0xf00) >> 7) + xSignBit);
      os.write(absX & 0xff);
    } else if (absX < 65 && absY < 65) {
      flagStream.write(onCurveBit + 20 + ((absX - 1) & 0x30) + (((absY - 1) & 0x30) >> 2) +
          xySignBits);
      os.write((((absX - 1) & 0xf) << 4) | ((absY - 1) & 0xf));
    } else if (absX < 769 && absY < 769) {
      flagStream.write(onCurveBit + 84 + 12 * (((absX - 1) & 0x300) >> 8) +
          (((absY - 1) & 0x300) >> 6) + xySignBits);
      os.write((absX - 1) & 0xff);
      os.write((absY - 1) & 0xff);
    } else if (absX < 4096 && absY < 4096) {
      flagStream.write(onCurveBit + 120 + xySignBits);
      os.write(absX >> 4);
      os.write(((absX & 0xf) << 4) | (absY >> 8));
      os.write(absY & 0xff);
    } else {
      flagStream.write(onCurveBit + 124 + xySignBits);
      os.write(absX >> 8);
      os.write(absX & 0xff);
      os.write(absY >> 8);
      os.write(absY & 0xff);
    }
  }

  /**
   * Split the instructions into a push sequence and the remainder of the instructions.
   * Writes both streams, and the counts to the glyfStream.
   *
   * @param glyph
   */
  private void splitPush(Glyph glyph) throws IOException {
    int instrSize = glyph.instructionSize();
    ReadableFontData data = glyph.instructions();
    int i = 0;
    List<Integer> result = new ArrayList<Integer>();
    // All push sequences are at least two bytes, make sure there's enough room
    while (i + 1 < instrSize) {
      int ix = i;
      int instr = data.readUByte(ix++);
      int n = 0;
      int size = 0;
      if (instr == 0x40 || instr == 0x41) {
        // NPUSHB, NPUSHW
        n = data.readUByte(ix++);
        size = (instr & 1) + 1;
      } else if (instr >= 0xB0 && instr < 0xC0) {
        // PUSHB, PUSHW
        n = 1 + (instr & 7);
        size = ((instr & 8) >> 3) + 1;
      } else {
        break;
      }
      if (i + size * n > instrSize) {
        // This is a broken font, and a potential buffer overflow, but in the interest
        // of preserving the original, we just put the broken instruction sequence in
        // the stream.
        break;
      }
      for (int j = 0; j < n; j++) {
        if (size == 1) {
          result.add(data.readUByte(ix));
        } else {
          result.add(data.readShort(ix));
        }
        ix += size;
      }
      i = ix;
    }
    int pushCount = result.size();
    int codeSize = instrSize - i;
    write255UShort(glyfStream, pushCount);
    write255UShort(glyfStream, codeSize);
    encodePushSequence(pushStream, result);
    if (codeSize > 0) {
      data.slice(i).copyTo(codeStream);
    }
  }

  // As per section 6.2.2 of the spec
  private void encodePushSequence(ByteArrayOutputStream os, List<Integer> data) throws IOException {
    int n = data.size();
    int hopSkip = 0;
    for (int i = 0; i < n; i++) {
      if ((hopSkip & 1) == 0) {
        int val = data.get(i);
        if (doHop && hopSkip == 0 && i >= 2 &&
            i + 2 < n && val == data.get(i - 2) && val == data.get(i + 2)) {
          if (i + 4 < n && val == data.get(i + 4)) {
            // Hop4 code
            os.write(252);
            hopSkip = 0x14;
          } else {
            // Hop3 code
            os.write(251);
            hopSkip = 4;
          }
        } else {
          if (push2byte) {
            // Measure relative effectiveness of 255Short literal encoding vs 2-byte ushort.
            writeUShort(os, data.get(i));
          } else {
            write255Short(os, data.get(i));
          }
        }
      }
      hopSkip >>= 1;
    }
  }

  public byte[] getGlyfBytes() {
    if (reslice) {
      ByteArrayOutputStream newStream = new ByteArrayOutputStream();
      try {
        // Pack all the glyf streams in a sensible way
        writeLong(newStream, 0);  // version
        writeUShort(newStream, nGlyphs);
        writeUShort(newStream, indexFmt.value());
        writeLong(newStream, nContourStream.size());
        writeLong(newStream, nPointsStream.size());
        writeLong(newStream, flagBytesStream.size());
        writeLong(newStream, glyfStream.size());
        writeLong(newStream, compositeStream.size());
        writeLong(newStream, bboxBitmap.length + bboxStream.size());
        writeLong(newStream, codeStream.size());
//        System.out.printf("stream sizes = %d %d %d %d %d %d %d\n",
//            nContourStream.size(), nPointsStream.size(), flagBytesStream.size(), glyfStream.size(),
//            compositeStream.size(), bboxStream.size(), codeStream.size());
        nContourStream.writeTo(newStream);
        nPointsStream.writeTo(newStream);
        flagBytesStream.writeTo(newStream);
        glyfStream.writeTo(newStream);
        compositeStream.writeTo(newStream);
        newStream.write(bboxBitmap);
        bboxStream.writeTo(newStream);
        codeStream.writeTo(newStream);
      } catch (IOException e) {
        throw new RuntimeException("Can't happen, world must have come to end", e);
      }
      return newStream.toByteArray();
    } else {
      return glyfStream.toByteArray();
    }
  }

  public byte[] getPushBytes() {
    return pushStream.toByteArray();
  }

  public byte[] getCodeBytes() {
    return codeStream.toByteArray();
  }

  public byte[] getLocaBytes() {
    return new byte[]{ };
  }
}
