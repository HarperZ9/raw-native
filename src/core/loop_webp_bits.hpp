#pragma once
// Private to src/core/loop_webp*.cpp: the VP8L bit writer and prefix (Huffman) codes. Bits go out
// least significant first (WebP lossless bitstream specification, RFC 9649 section 3).
#include <cstdint>
#include <string>
#include <vector>
namespace raw::media::vp8l {

struct BitWriter {
    std::string bytes; std::uint64_t acc{0}; int n{0};
    void put(std::uint32_t v, int bits) {
        if (bits == 0) return;
        acc |= std::uint64_t(v & ((bits == 32) ? 0xFFFFFFFFu : ((1u << bits) - 1u))) << n;
        n += bits;
        while (n >= 8) { bytes.push_back(char(acc & 0xFF)); acc >>= 8; n -= 8; }
    }
    void flush() { while (n > 0) { bytes.push_back(char(acc & 0xFF)); acc >>= 8; n -= 8; } n = 0; acc = 0; }
};

// A prefix code over an alphabet: per-symbol lengths and bit-reversed canonical codes, so writing
// `codes[s]` in `lengths[s]` bits least significant first sends the code's first bit first.
struct PrefixCode {
    std::vector<int> lengths;
    std::vector<std::uint32_t> codes;
    bool simple{false};              // written as a simple code (1 or 2 symbols, both < 256)
    std::vector<int> simpleSymbols;
};

// Lengths limited to `limit` bits from a histogram; at least two symbols get a length when any is
// used (a one-symbol normal code would be read as a zero-bit code).
std::vector<int> limitedLengths(std::vector<std::uint32_t> hist, int limit);
void assignCodes(PrefixCode& c);
// Builds the code for a histogram (simple when it can be) and writes its description.
PrefixCode buildCode(const std::vector<std::uint32_t>& hist);
void writeCode(BitWriter& bw, const PrefixCode& c);
inline void emit(BitWriter& bw, const PrefixCode& c, int sym) {
    if (c.simple) { if (c.simpleSymbols.size() == 2) bw.put(sym == c.simpleSymbols[1] ? 1u : 0u, 1); return; }
    bw.put(c.codes[std::size_t(sym)], c.lengths[std::size_t(sym)]);
}

}  // namespace raw::media::vp8l
