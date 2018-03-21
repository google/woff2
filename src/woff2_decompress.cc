/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

/* A very simple commandline tool for decompressing woff2 format files to true
   type font files. */

#include "woff2_decompress.h"

int main(int argc, char **argv) {
  using std::string;

  if (argc != 2) {
    fprintf(stderr, "One argument, the input filename, must be provided.\n");
    return 1;
  }

  std::string argument = argv[1];
  if (argument == "--help" || argument == "-h") {
      std::cout << APPLICATION << std::endl;
      std::cout << AUTHOR << std::endl;
      std::cout << LICENSE << std::endl;
      std::cout << HELPSTRING << std::endl;
      std::cout << "\n" + USAGESTRING << std::endl;
      return 0;
  }

  if (argument == "--usage"){
      std::cout << USAGESTRING << std::endl;
      return 0;
  }

  if (argument == "--version" || argument == "-v") {
      std::cout << APPLICATION + " " + VERSION << std::endl;
      return 0;
  }

  string filename(argv[1]);
  string outfilename = filename.substr(0, filename.find_last_of(".")) + ".ttf";

  // Note: update woff2_dec_fuzzer_new_entry.cc if this pattern changes.
  string input = woff2::GetFileContent(filename);
  const uint8_t* raw_input = reinterpret_cast<const uint8_t*>(input.data());
  string output(std::min(woff2::ComputeWOFF2FinalSize(raw_input, input.size()),
                         woff2::kDefaultMaxSize), 0);
  woff2::WOFF2StringOut out(&output);

  const bool ok = woff2::ConvertWOFF2ToTTF(raw_input, input.size(), &out);

  if (ok) {
    woff2::SetFileContents(outfilename, output.begin(),
        output.begin() + out.Size());
  }
  return ok ? 0 : 1;
}
