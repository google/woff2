#!/bin/bash
#
# Tests that the compression and decompression shrinks and grows the file sizes appropriately

set -o errexit

function abs() {
  if [[ "$1" -lt 0 ]]; then
    echo $((-$1));
  else
    echo "$1"
  fi
}

function size() {
    echo $(stat -c%s "$1")
}

test_dir=$(dirname "$0")
roboto_ttf="${test_dir}/roboto/roboto.ttf"
tmp_dir="${test_dir}/tmp"
tmp_ttf="${tmp_dir}/test.ttf"
tmp_woff2="${tmp_dir}/test.woff2"

# Copy test font into tmp folder
mkdir -p "${tmp_dir}"
cp "${roboto_ttf}" "${tmp_ttf}"

# Convert the TTF to WOFF2
./woff2_compress "${tmp_ttf}"

roboto_size=$(size "${roboto_ttf}")
if (( roboto_size < $(size "${tmp_woff2}") )); then
  echo 'Expected TTF file to be larger than WOFF2 file'
  exit 1
fi

# Remove copied file
rm "${tmp_ttf}"

# Re-generate the original TTF
./woff2_decompress "${tmp_woff2}"
if (( $(abs $((( roboto_size - $(size "${tmp_ttf}") )))) > (roboto_size / 130) )); then
  echo 'Expected original file TTF to be close to decompressed file size'
  exit 2
fi
