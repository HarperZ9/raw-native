# Vendored: superstack v0.1.0 (MIT)

This directory holds files copied byte for byte from the `v0.1.0` tag of
[HarperZ9/superstack](https://github.com/HarperZ9/superstack), the shared
contract for renderers and sound engines:

- `superstack.hpp`: the C++23 implementation, header only, standard library only.
- `tests/run_vectors.cpp`: the contract's C++ vector runner.
- `vectors/`: the contract's test vectors, pinned by `vectors/MANIFEST.json`.

`LICENSE.txt` is not from the tag: it repeats the MIT notice from the top of
`superstack.hpp` so release archives, whose binaries contain that code, carry it.

**Licence.** These files are under the MIT licence, carried in full at the
top of `superstack.hpp` and `tests/run_vectors.cpp`. They keep that licence
inside this repository. The rest of raw-native is under FSL-1.1-MIT (see the
top-level `LICENSE`); the FSL does not apply to the files in this directory.

**Pin.** `SUPERSTACK.sha256` records the SHA-256 of each copied file, taken
from the tag (the header's hash equals the one in superstack's `SHA256SUMS`).
CI checks the pins on every push and runs the vectors against the header on
each compiler raw-native builds with. `.gitattributes` keeps these files at LF
line endings so a Windows checkout does not change their bytes.

**Do not edit these files.** A change belongs in superstack, under new
vectors, and arrives here as a new tag with new pins.
