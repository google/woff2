// Copyright 2011 Google Inc. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package com.google.typography.font.compression;

import com.google.common.collect.Lists;

import java.io.PrintWriter;
import java.util.Collections;
import java.util.List;

/**
 * Class for gathering up stats, for summarizing and graphing.
 *
 * @author raph@google.com (Raph Levien)
 */
public class StatsCollector {

  private final List<Double> values;

  public StatsCollector() {
    values = Lists.newArrayList();
  }

  public void addStat(double value) {
    values.add(value);
  }

  public double mean() {
    double sum = 0;
    for (Double value : values) {
      sum += value;
    }
    return sum / values.size();
  }

  public double median() {
    Collections.sort(values);
    int length = values.size();
    if (length % 2 == 1) {
      return values.get((length - 1) / 2);
    } else {
      return 0.5 * (values.get(length / 2 - 1) + values.get(length / 2));
    }
  }

  // Need to print <html> before calling this method
  public void chartHeader(PrintWriter o, int n) {
    o.println("<head>");
    o.println("<script type='text/javascript' src='https://www.google.com/jsapi'></script>");
    o.println("<script type='text/javascript'>");
    o.println("google.load('visualization', '1', {packages:['corechart']});");
    o.println("google.setOnLoadCallback(drawChart);");
    o.println("function drawChart() {");
    o.println("  var data = new google.visualization.DataTable()");
    o.println("  data.addColumn('string', 'Font');");
    if (n == 1) {
      o.println("  data.addColumn('number', 'Ratio');");
    } else {
      for (int i = 0; i < n; i++) {
        o.printf("  data.addColumn('number', 'Ratio %c');\n", 'A' + i);
      }
    }
    o.printf("  data.addRows(%d);\n", values.size());
  }

  public void chartData(PrintWriter o, int ix) {
    Collections.sort(values);
    int length = values.size();
    for (int i = 0; i < length; i++) {
      o.printf("  data.setValue(%d, %d, %f);\n", i, ix, values.get(i));
    }
  }

  public void chartEnd(PrintWriter o) {
    o.println("  var chart = new google.visualization.LineChart(document.getElementById("
        + "'chart_div'));");
    o.println("  chart.draw(data, {width:700, height:400, title: 'Compression ratio'});");
    o.println("}");
    o.println("</script>");
    o.println("</head>");

    o.println();
    o.println("<body>");
    o.println("<div id='chart_div'></div>");
    // TODO: split so we can get content into the HTML
  }
  public void chartFooter(PrintWriter o) {
    o.println("</body>");
    o.println("</html>");
  }
}
