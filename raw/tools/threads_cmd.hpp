#pragma once
// raw_native_cli threads: render the Threads module on the build's GPU backend
// and write the last frame as threads.ppm. Exit 0 rendered, 2 bad input or a
// backend error, 4 no GPU backend or adapter.
namespace raw {
int threadsCommand(int argc, char** argv);
}
