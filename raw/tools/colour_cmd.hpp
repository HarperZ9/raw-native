#pragma once
// raw_native_cli colour: the colour reference's command line (raw/renderer/colour.hpp).
//   colour list                          the pipeline names
//   colour grid OUT                      the committed input grid, float32 RGB triples
//   colour apply PIPELINE IN OUT         run a pipeline over float32 RGB triples
//   colour tables PIPELINE OUT.json      ACES 2.0 parameters and tables for the GPU
// Exit 0 done, 2 bad input or an unwritable file.
namespace raw {
int colourCommand(int argc, char** argv);
}
