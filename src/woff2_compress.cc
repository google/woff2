/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* A commandline tool for compressing ttf format files to woff2. */

#include "woff2_compress.h"

int main(int argc, char **argv) {
  using std::string;

  if (argc != 2) {
    fprintf(stderr, "Please include an argument with your command.\n");
    fprintf(stderr, "%s\n", USAGESTRING.c_str());
    return 1;
  }

  string argument = argv[1];
  if (argument == "--help" || argument == "-h") {
    fprintf(stdout, "%s\n", APPLICATION.c_str());
    fprintf(stdout, "%s\n", AUTHOR.c_str());
    fprintf(stdout, "%s\n", LICENSE.c_str());
    fprintf(stdout, "\n%s\n", HELPSTRING.c_str());
    fprintf(stdout, "\n%s\n", USAGESTRING.c_str());
    return 0;
  }
  
  if (argument == "--usage") {
    fprintf(stdout, "%s\n", USAGESTRING.c_str());
    return 0;
  }

  if (argument == "--version" || argument == "-v") {
    fprintf(stdout, "%s %s\n", APPLICATION.c_str(), VERSION.c_str());
    return 0;
  }

  string filename(argv[1]);
  string outfilename = filename.substr(0, filename.find_last_of(".")) + ".woff2";
  fprintf(stdout, "Processing %s => %s\n",
    filename.c_str(), outfilename.c_str());
  string input = woff2::GetFileContent(filename);

  const uint8_t* input_data = reinterpret_cast<const uint8_t*>(input.data());
  size_t output_size = woff2::MaxWOFF2CompressedSize(input_data, input.size());
  string output(output_size, 0);
  uint8_t* output_data = reinterpret_cast<uint8_t*>(&output[0]);

  woff2::WOFF2Params params;
  if (!woff2::ConvertTTFToWOFF2(input_data, input.size(),
                                output_data, &output_size, params)) {
    fprintf(stderr, "Compression failed.\n");
    return 1;
  }
  output.resize(output_size);

  woff2::SetFileContents(outfilename, output.begin(), output.end());

  return 0;
}
