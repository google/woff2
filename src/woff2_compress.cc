// Copyright 2013 Google Inc. All Rights Reserved.
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
// A commandline tool for compressing ttf format files to woff2.

#include <string>

#include "file.h"
#include "./woff2_enc.h"


int main(int argc, char **argv) {
  using std::string;

  if (argc < 2) {
    fprintf(stderr, "At least one argument, the input filename, must be provided.\n");
    return 1;
  }

  if (argc > 3) {
    fprintf(stderr, "A maximum of two arguments, can be provided, the input filename and the output one.\n");
    return 1;
  }

  string filename(argv[1]);
  string outfilename =  argc == 2 ?
    filename.substr(0, filename.find_last_of(".")) + ".woff2" :
    argv[2];
  argc == 2 && fprintf(stdout, "Processing %s => %s\n",
    filename.c_str(), outfilename.c_str());
  string input = woff2::GetFileContent(filename);

  const uint8_t* input_data = reinterpret_cast<const uint8_t*>(input.data());
  size_t output_size = woff2::MaxWOFF2CompressedSize(input_data, input.size());
  string output(output_size, 0);
  uint8_t* output_data = reinterpret_cast<uint8_t*>(&output[0]);

  if (!woff2::ConvertTTFToWOFF2(input_data, input.size(),
                                output_data, &output_size)) {
    fprintf(stderr, "Compression failed.\n");
    return 1;
  }
  output.resize(output_size);

  woff2::SetFileContents(outfilename, output);

  return 0;
}
