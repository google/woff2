package com.google.typography.font.compression;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Lists;
import com.google.common.collect.Maps;

import java.util.Collections;
import java.util.List;
import java.util.Map;

/**
 * Compression stats, both aggregate and per font.
 *
 * @author David Kuettel
 */
public class CompressionStats {

  public enum Size { ORIGINAL, GZIP, WOFF, WOFF2 }

  private final List<Stats> values = Lists.newArrayList();

  public void add(Stats stat) {
    values.add(stat);
  }

  public List<Stats> values() {
    return values;
  }

  public double mean(Size size) {
    double sum = 0;
    for (Long value : values(size)) {
      sum += value;
    }
    return sum / values.size();
  }

  public double median(Size size) {
    List<Long> list = values(size);
    Collections.sort(list);
    int length = list.size();
    if (length % 2 == 1) {
      return list.get((length - 1) / 2);
    } else {
      return 0.5 * (list.get(length / 2 - 1) + list.get(length / 2));
    }
  }

  private List<Long> values(Size size) {
    List<Long> list = Lists.newArrayList();
    for (Stats stats : values) {
      list.add(stats.getSize(size));
    }
    return list;
  }

  public static class Stats {

    private final String filename;
    private final Map<Size, Long> sizes;

    private Stats(String filename, Map<Size, Long> sizes) {
      this.filename = filename;
      this.sizes = sizes;
    }

    public String getFilename() {
      return filename;
    }

    public long getSize(Size size) {
      return sizes.get(size);
    }

    public double getPercent(Size s1, Size s2) {
      long v1 = sizes.get(s1);
      long v2 = sizes.get(s2);
      return 100.0 * (v1 - v2) / v1;
    }

    public static Builder builder() {
      return new Builder();
    }

    public static class Builder {

      private String filename;
      private Map<Size, Long> sizes = Maps.newHashMap();

      public Builder setFilename(String filename) {
        this.filename = filename;
        return this;
      }

      public Builder setSize(Size key, long value) {
        this.sizes.put(key, value);
        return this;
      }

      public Stats build() {
        return new Stats(filename, ImmutableMap.copyOf(sizes));
      }
    }
  }
}
