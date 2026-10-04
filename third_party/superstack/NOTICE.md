# Vendored: superstack v0.2.0 (FSL-1.1-MIT)

This directory holds files copied byte for byte from the `v0.2.0` tag of
[HarperZ9/superstack](https://github.com/HarperZ9/superstack), the shared
contract for renderers and sound engines:

- `superstack.hpp`: the C++23 implementation, header only, standard library only.
- `tests/run_vectors.cpp`: the contract's C++ vector runner.
- `vectors/`: the contract's test vectors, pinned by `vectors/MANIFEST.json`.
  The vectors are the same bytes in v0.1.0 and v0.2.0.

`LICENSE.txt` is not from the tag. It repeats the notice from the top of
`superstack.hpp`, followed by the full FSL-1.1-MIT text from superstack's
`LICENSE` at the same tag, so release archives, whose binaries contain that
code, carry both. Release archives ship it as `LICENSE-superstack.txt`.

**Licence.** From v0.2.0 these files are under the Functional Source License,
Version 1.1, MIT Future License (`FSL-1.1-MIT`), the same licence as the rest
of raw-native. Each superstack release becomes available under MIT two years
after it ships. superstack v0.1.0, which raw-native 0.5.0 vendored, remains
under MIT for anyone who took it. The header names the algorithms by others
that it contains (mulberry32, xmur3, the OKLab matrices and the libebur128
K-weighting constants). Each keeps its own terms, which the FSL does not
change, and their notices are kept word for word in `LICENSE.txt`.

**Pin.** `SUPERSTACK.sha256` records the SHA-256 of each copied file, taken
from the tag (the header's hash equals the one in superstack's `SHA256SUMS`).
CI checks the pins on every push and runs the vectors against the header on
each compiler raw-native builds with. `.gitattributes` keeps these files at LF
line endings so a Windows checkout does not change their bytes.

**Do not edit these files.** A change belongs in superstack, under new
vectors, and arrives here as a new tag with new pins.
