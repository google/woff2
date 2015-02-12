// Copyright 2014 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Common definition for WOFF2 encoding/decoding

#ifndef WOFF2_WOFF2_COMMON_H_
#define WOFF2_WOFF2_COMMON_H_

#include <inttypes.h>

namespace woff2 {

static const uint32_t kWoff2Signature = 0x774f4632;  // "wOF2"

const unsigned int kWoff2FlagsContinueStream = 1 << 4;
const unsigned int kWoff2FlagsTransform = 1 << 5;

struct Point {
  int x;
  int y;
  bool on_curve;
};

struct Table {
  uint32_t tag;
  uint32_t flags;
  uint32_t src_offset;
  uint32_t src_length;

  uint32_t transform_length;

  uint32_t dst_offset;
  uint32_t dst_length;
  const uint8_t* dst_data;

  bool operator<( const Table& other ) const { 
    return tag < other.tag;
  }

};

} // namespace woff2

#endif  // WOFF2_WOFF2_COMMON_H_
