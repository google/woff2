// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.typography.font.sfntly.Font;
import com.google.typography.font.sfntly.Tag;
import com.google.typography.font.sfntly.data.WritableFontData;
import com.google.typography.font.sfntly.table.core.HorizontalMetricsTable;

/**
 * Extract just advance widths from hmtx table.
 *
 * @author Raph Levien
 */
public class AdvWidth {

  public static WritableFontData encode(Font font) {
    HorizontalMetricsTable hmtx = font.getTable(Tag.hmtx);
    int nMetrics = hmtx.numberOfHMetrics();
    WritableFontData result = WritableFontData.createWritableFontData(nMetrics * 2);
    for (int i = 0; i < nMetrics; i++) {
      result.writeShort(i * 2, hmtx.hMetricAdvanceWidth(i));
    }
    return result;
  }
}
