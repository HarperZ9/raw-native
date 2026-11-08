// VP8L prefix codes for the WebP loop writer: see loop_webp_bits.hpp. Code lengths come from a
// Huffman tree; when the tree is too deep, small counts are raised (doubling a floor) and the tree
// is rebuilt, the way libwebp limits lengths. The code-length code and its run tokens follow
// RFC 9649 section 3.7.2.1.
#include "loop_webp_bits.hpp"
#include <algorithm>
#include <array>
#include <queue>
#include <stdexcept>
namespace raw::media::vp8l {
namespace {
constexpr std::array<int, 19> kOrder = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

std::vector<int> treeDepths(const std::vector<std::uint64_t>& w) {
    struct Node { std::uint64_t w; int id; };
    auto cmp = [](const Node& a, const Node& b) { return a.w != b.w ? a.w > b.w : a.id > b.id; };
    std::priority_queue<Node, std::vector<Node>, decltype(cmp)> q(cmp);
    std::vector<int> parent;
    std::vector<int> leaf(w.size(), -1);
    for (std::size_t s = 0; s < w.size(); ++s) if (w[s] > 0) { leaf[s] = int(parent.size()); q.push({w[s], int(parent.size())}); parent.push_back(-1); }
    while (q.size() > 1) {
        const Node a = q.top(); q.pop();
        const Node b = q.top(); q.pop();
        const int id = int(parent.size());
        parent.push_back(-1);
        parent[std::size_t(a.id)] = id; parent[std::size_t(b.id)] = id;
        q.push({a.w + b.w, id});
    }
    std::vector<int> depth(w.size(), 0);
    for (std::size_t s = 0; s < w.size(); ++s) {
        if (leaf[s] < 0) continue;
        int d = 0;
        for (int n = leaf[s]; parent[std::size_t(n)] >= 0; n = parent[std::size_t(n)]) ++d;
        depth[s] = d;
    }
    return depth;
}
std::uint32_t reverse(std::uint32_t v, int bits) { std::uint32_t r = 0; for (int i = 0; i < bits; ++i) { r = (r << 1) | (v & 1); v >>= 1; } return r; }

struct Token { int sym, extra, extraBits; };
std::vector<Token> tokens(const std::vector<int>& len) {
    std::vector<Token> t;
    for (std::size_t i = 0; i < len.size();) {
        std::size_t j = i;
        while (j < len.size() && len[j] == len[i]) ++j;
        std::size_t run = j - i;
        if (len[i] == 0) {
            while (run >= 11) { const std::size_t r = std::min<std::size_t>(run, 138); t.push_back({18, int(r - 11), 7}); run -= r; }
            if (run >= 3) { t.push_back({17, int(run - 3), 3}); run = 0; }
            while (run-- > 0) t.push_back({0, 0, 0});
        } else {
            t.push_back({len[i], 0, 0}); --run;
            while (run >= 3) { const std::size_t r = std::min<std::size_t>(run, 6); t.push_back({16, int(r - 3), 2}); run -= r; }
            while (run-- > 0) t.push_back({len[i], 0, 0});
        }
        i = j;
    }
    return t;
}
}  // namespace

std::vector<int> limitedLengths(std::vector<std::uint32_t> hist, int limit) {
    int used = 0;
    for (std::uint32_t h : hist) used += h > 0;
    if (used == 0) return std::vector<int>(hist.size(), 0);
    if (used == 1) for (std::size_t s = 0; s < hist.size(); ++s) if (hist[s] == 0) { hist[s] = 1; break; }   // a dummy partner
    for (std::uint64_t floor = 1;; floor *= 2) {
        std::vector<std::uint64_t> w(hist.size());
        for (std::size_t s = 0; s < hist.size(); ++s) w[s] = hist[s] ? std::max<std::uint64_t>(hist[s], floor) : 0;
        std::vector<int> d = treeDepths(w);
        if (*std::max_element(d.begin(), d.end()) <= limit) return d;
    }
}

void assignCodes(PrefixCode& c) {
    int maxLen = 0;
    for (int l : c.lengths) maxLen = std::max(maxLen, l);
    std::vector<std::uint32_t> count(std::size_t(maxLen) + 1, 0), next(std::size_t(maxLen) + 2, 0);
    for (int l : c.lengths) if (l) ++count[std::size_t(l)];
    std::uint32_t code = 0;
    for (int l = 1; l <= maxLen; ++l) { code = (code + count[std::size_t(l) - 1]) << 1; next[std::size_t(l)] = code; }
    c.codes.assign(c.lengths.size(), 0);
    for (std::size_t s = 0; s < c.lengths.size(); ++s) if (const int l = c.lengths[s]) c.codes[s] = reverse(next[std::size_t(l)]++, l);
}

PrefixCode buildCode(const std::vector<std::uint32_t>& hist) {
    PrefixCode c;
    std::vector<int> used;
    for (std::size_t s = 0; s < hist.size(); ++s) if (hist[s]) used.push_back(int(s));
    if (used.empty()) used.push_back(0);
    if (used.size() <= 2 && used.back() < 256) { c.simple = true; c.simpleSymbols = used; return c; }
    c.lengths = limitedLengths(hist, 15);
    assignCodes(c);
    return c;
}

void writeCode(BitWriter& bw, const PrefixCode& c) {
    if (c.simple) {
        bw.put(1, 1);
        bw.put(std::uint32_t(c.simpleSymbols.size() - 1), 1);
        const int s0 = c.simpleSymbols[0];
        if (s0 < 2) { bw.put(0, 1); bw.put(std::uint32_t(s0), 1); } else { bw.put(1, 1); bw.put(std::uint32_t(s0), 8); }
        if (c.simpleSymbols.size() == 2) bw.put(std::uint32_t(c.simpleSymbols[1]), 8);
        return;
    }
    bw.put(0, 1);
    const std::vector<Token> t = tokens(c.lengths);
    std::vector<std::uint32_t> h(19, 0);
    for (const Token& k : t) ++h[std::size_t(k.sym)];
    PrefixCode cl;
    cl.lengths = limitedLengths(h, 7);
    assignCodes(cl);
    int num = 4;
    for (int i = 0; i < 19; ++i) if (cl.lengths[std::size_t(kOrder[std::size_t(i)])]) num = std::max(num, i + 1);
    bw.put(std::uint32_t(num - 4), 4);
    for (int i = 0; i < num; ++i) bw.put(std::uint32_t(cl.lengths[std::size_t(kOrder[std::size_t(i)])]), 3);
    bw.put(0, 1);   // max_symbol: the whole alphabet
    for (const Token& k : t) { bw.put(cl.codes[std::size_t(k.sym)], cl.lengths[std::size_t(k.sym)]); bw.put(std::uint32_t(k.extra), k.extraBits); }
}

}  // namespace raw::media::vp8l
