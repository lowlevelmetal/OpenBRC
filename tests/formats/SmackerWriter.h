#pragma once

// A small Smacker encoder for tests: it produces files that exercise every
// feature of the format (all block types and SMK4 full-block modes, runs,
// palette commands, ring frames, Y scaling, raw and compressed audio on any
// track, keyframes) so the decoder can be checked by round trip. It favours
// simplicity over compression: Huffman trees are balanced over the values
// actually used and the escape (MRU) values are never used.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace brc::test::smk {

class BitWriter {
public:
    void bit(unsigned b) {
        if (m_n % 8 == 0)
            m_bytes.push_back(0);
        if (b)
            m_bytes.back() |= static_cast<std::uint8_t>(1u << (m_n % 8));
        ++m_n;
    }
    void bits(std::uint32_t v, int n) {
        for (int i = 0; i < n; ++i)
            bit((v >> i) & 1u);
    }
    void code(const std::vector<std::uint8_t>& path) {
        for (auto b : path)
            bit(b);
    }
    std::vector<std::uint8_t> take() { return std::move(m_bytes); }

private:
    std::vector<std::uint8_t> m_bytes;
    std::size_t m_n = 0;
};

// Balanced prefix tree over a sorted set of values.
struct Codes {
    std::map<std::uint32_t, std::vector<std::uint8_t>> path;
    std::size_t entries = 0; // flat-array size of the tree

    template <class LeafFn>
    void writeTree(BitWriter& w, const std::vector<std::uint32_t>& values, std::size_t lo, std::size_t hi,
                   std::vector<std::uint8_t>& prefix, LeafFn&& leaf) {
        ++entries;
        if (hi - lo == 1) {
            w.bit(0);
            leaf(values[lo]);
            path[values[lo]] = prefix;
            return;
        }
        w.bit(1);
        const std::size_t mid = lo + (hi - lo) / 2;
        prefix.push_back(0);
        writeTree(w, values, lo, mid, prefix, leaf);
        prefix.back() = 1;
        writeTree(w, values, mid, hi, prefix, leaf);
        prefix.pop_back();
    }
};

// 8-bit tree: presence bit, tree, terminating bit.
inline Codes writeSmallTree(BitWriter& w, const std::set<std::uint32_t>& used) {
    Codes c;
    if (used.empty()) {
        w.bit(0);
        return c;
    }
    w.bit(1);
    const std::vector<std::uint32_t> values(used.begin(), used.end());
    std::vector<std::uint8_t> prefix;
    c.writeTree(w, values, 0, values.size(), prefix, [&](std::uint32_t v) { w.bits(v, 8); });
    w.bit(0);
    return c;
}

// 16-bit tree: presence bit, low/high byte trees, three escapes, tree, end bit.
struct BigCodes {
    Codes lo, hi, tree;
    bool absent = false;
    std::uint32_t allocSize = 16;

    void put(BitWriter& w, std::uint32_t v) const {
        if (absent)
            return;
        w.code(tree.path.at(v));
    }
};

inline BigCodes writeBigTree(BitWriter& w, const std::set<std::uint32_t>& used) {
    BigCodes c;
    if (used.empty()) {
        w.bit(0);
        c.absent = true;
        return c;
    }
    w.bit(1);
    std::set<std::uint32_t> los, his;
    for (auto v : used) {
        los.insert(v & 0xff);
        his.insert(v >> 8);
    }
    c.lo = writeSmallTree(w, los);
    c.hi = writeSmallTree(w, his);
    // Escapes: values never used, so the MRU slots are separate entries.
    std::uint32_t esc = 0;
    for (int i = 0; i < 3; ++i) {
        while (used.contains(esc))
            ++esc;
        w.bits(esc++, 16);
    }
    const std::vector<std::uint32_t> values(used.begin(), used.end());
    std::vector<std::uint8_t> prefix;
    c.tree.writeTree(w, values, 0, values.size(), prefix, [&](std::uint32_t v) {
        w.code(c.lo.path.at(v & 0xff));
        w.code(c.hi.path.at(v >> 8));
    });
    w.bit(0);
    c.allocSize = static_cast<std::uint32_t>((c.tree.entries + 3) * 4);
    return c;
}

struct AudioTrack {
    bool compressed = false;
    bool is16 = false;
    bool stereo = false;
    std::uint32_t rate = 22050;
};

struct Frame {
    std::vector<std::uint8_t> pixels;                     // width x height (stored height)
    std::optional<std::array<std::uint8_t, 768>> palette; // 8-bit components from the 6-bit table
    std::array<std::vector<std::int32_t>, 7> audio;       // raw source samples: 0..255 or int16, interleaved
    bool keyframe = false;
};

struct Movie {
    int version = 2;
    std::uint32_t width = 16, height = 8;
    std::int32_t rate = 100;
    std::uint32_t flags = 0; // bit 0 ring frame (frames.back() is then the ring frame), 1 interlace, 2 double
    std::array<std::optional<AudioTrack>, 7> tracks;
    std::vector<Frame> frames; // stored frames, including the ring frame
};

inline constexpr std::array<std::uint8_t, 64> kPal6to8 = {
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 0x3C,
    0x41, 0x45, 0x49, 0x4D, 0x51, 0x55, 0x59, 0x5D, 0x61, 0x65, 0x69, 0x6D, 0x71, 0x75, 0x79, 0x7D,
    0x82, 0x86, 0x8A, 0x8E, 0x92, 0x96, 0x9A, 0x9E, 0xA2, 0xA6, 0xAA, 0xAE, 0xB2, 0xB6, 0xBA, 0xBE,
    0xC3, 0xC7, 0xCB, 0xCF, 0xD3, 0xD7, 0xDB, 0xDF, 0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF};

inline std::uint8_t to6(std::uint8_t v8) {
    for (std::uint8_t i = 0; i < 64; ++i)
        if (kPal6to8[i] == v8)
            return i;
    return 0;
}

// Palette chunk (with its size byte and padding) turning `old` into `pal`:
// skips for unchanged entries, copies for entries found elsewhere in `old`,
// new entries otherwise.
inline std::vector<std::uint8_t> encodePalette(const std::array<std::uint8_t, 768>& old,
                                               const std::array<std::uint8_t, 768>& pal) {
    std::vector<std::uint8_t> c{0};
    auto same = [&](int a, const std::array<std::uint8_t, 768>& p, int b) {
        return std::memcmp(&pal[static_cast<std::size_t>(a) * 3], &p[static_cast<std::size_t>(b) * 3], 3) ==
               0;
    };
    int i = 0;
    while (i < 256) {
        int n = 0;
        while (i + n < 256 && n < 128 && same(i + n, old, i + n))
            ++n;
        if (n > 0) {
            c.push_back(static_cast<std::uint8_t>(0x80 | (n - 1)));
            i += n;
            continue;
        }
        int src = -1;
        for (int j = 0; j < 256 && src < 0; ++j)
            if (same(i, old, j))
                src = j;
        if (src >= 0) {
            int len = 1;
            while (len < 64 && i + len < 256 && src + len < 256 && same(i + len, old, src + len) &&
                   !same(i + len, old, i + len))
                ++len;
            c.push_back(static_cast<std::uint8_t>(0x40 | (len - 1)));
            c.push_back(static_cast<std::uint8_t>(src));
            i += len;
            continue;
        }
        const auto* e = &pal[static_cast<std::size_t>(i) * 3];
        c.push_back(to6(e[0]));
        c.push_back(to6(e[1]));
        c.push_back(to6(e[2]));
        ++i;
    }
    while (c.size() % 4)
        c.push_back(0);
    c[0] = static_cast<std::uint8_t>(c.size() / 4);
    return c;
}

inline void putLE32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i)
        v.push_back(static_cast<std::uint8_t>(x >> (8 * i)));
}

// Audio chunk payload (without the length word).
inline std::vector<std::uint8_t> encodeAudio(const AudioTrack& t, const std::vector<std::int32_t>& s) {
    std::vector<std::uint8_t> out;
    const std::size_t ch = t.stereo ? 2 : 1;
    if (!t.compressed) {
        for (auto v : s) {
            out.push_back(static_cast<std::uint8_t>(v));
            if (t.is16)
                out.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> 8));
        }
        return out;
    }
    putLE32(out, static_cast<std::uint32_t>(s.size() * (t.is16 ? 2 : 1)));
    BitWriter w;
    w.bit(1);
    w.bit(t.stereo);
    w.bit(t.is16);
    // Deltas per channel (and per byte for 16-bit).
    const std::size_t ntrees = (t.is16 ? 2u : 1u) * ch;
    std::vector<std::set<std::uint32_t>> used(ntrees);
    std::vector<std::vector<std::uint32_t>> syms; // per sample: tree symbols
    std::array<std::int32_t, 2> prev{};
    for (std::size_t i = 0; i < s.size(); ++i) {
        const std::size_t c = i % ch;
        if (i < ch) {
            prev[c] = s[i];
            syms.push_back({});
            continue;
        }
        if (t.is16) {
            const auto d = static_cast<std::uint32_t>(s[i] - prev[c]) & 0xffff;
            used[c * 2].insert(d & 0xff);
            used[c * 2 + 1].insert(d >> 8);
            syms.push_back({d & 0xff, d >> 8});
        } else {
            const auto d = static_cast<std::uint32_t>(s[i] - prev[c]) & 0xff;
            used[c].insert(d);
            syms.push_back({d});
        }
        prev[c] = s[i];
    }
    std::vector<Codes> codes;
    for (std::size_t k = 0; k < ntrees; ++k)
        codes.push_back(writeSmallTree(w, used[k]));
    for (std::size_t c = ch; c-- > 0;) {
        const auto v = static_cast<std::uint32_t>(c < s.size() ? s[c] : 0);
        if (t.is16)
            w.bits(((v & 0xff) << 8) | ((v >> 8) & 0xff), 16);
        else
            w.bits(v & 0xff, 8);
    }
    for (std::size_t i = ch; i < s.size(); ++i) {
        const std::size_t c = i % ch;
        if (t.is16) {
            w.code(codes[c * 2].path.at(syms[i][0]));
            w.code(codes[c * 2 + 1].path.at(syms[i][1]));
        } else {
            w.code(codes[c].path.at(syms[i][0]));
        }
    }
    const auto bits = w.take();
    out.insert(out.end(), bits.begin(), bits.end());
    return out;
}

// One coded block run.
struct Run {
    std::uint32_t type; // big-tree "type" value
    std::vector<std::pair<int, std::uint32_t>>
        data; // (tree index, value) in bitstream order; tree -1 = raw bit
};

inline constexpr std::array<std::uint32_t, 64> kRuns = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,  17,  18,  19,   20,  21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  39,  40,  41,   42,  43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048};

enum { kMMap, kMClr, kFull, kType };

inline std::vector<Run> encodeVideo(const Movie& m, const std::vector<std::uint8_t>& prev,
                                    const std::vector<std::uint8_t>& cur) {
    const std::uint32_t bw = m.width / 4, bh = m.height / 4;
    struct Block {
        int kind;
        int mode = 0;
        std::uint32_t color = 0;
        std::vector<std::pair<int, std::uint32_t>> data;
    };
    std::vector<Block> blocks;
    for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
            std::array<std::uint8_t, 16> px{};
            bool changed = false;
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const std::size_t o = (by * 4 + static_cast<std::uint32_t>(y)) * m.width + bx * 4 +
                                          static_cast<std::uint32_t>(x);
                    px[static_cast<std::size_t>(y * 4 + x)] = cur[o];
                    changed |= prev.empty() || prev[o] != cur[o];
                }
            const std::set<std::uint8_t> colors(px.begin(), px.end());
            Block b{};
            if (!changed) {
                b.kind = 2;
            } else if (colors.size() == 1) {
                b.kind = 3;
                b.color = px[0];
            } else if (colors.size() == 2) {
                b.kind = 0;
                const std::uint8_t lo = *colors.begin(), hi = *colors.rbegin();
                std::uint32_t map = 0;
                for (int i = 0; i < 16; ++i)
                    if (px[static_cast<std::size_t>(i)] == hi)
                        map |= 1u << i;
                b.data = {{kMClr, static_cast<std::uint32_t>(hi << 8 | lo)}, {kMMap, map}};
            } else {
                b.kind = 1;
                auto pair = [&](int y, int x) {
                    return static_cast<std::uint32_t>(px[static_cast<std::size_t>(y * 4 + x)] |
                                                      px[static_cast<std::size_t>(y * 4 + x + 1)] << 8);
                };
                auto rowsEqual = [&](int a, int b2) {
                    return std::equal(px.begin() + a * 4, px.begin() + a * 4 + 4, px.begin() + b2 * 4);
                };
                const bool rowsDoubled = rowsEqual(0, 1) && rowsEqual(2, 3);
                const bool pixelsDoubled =
                    rowsDoubled && px[0] == px[1] && px[2] == px[3] && px[8] == px[9] && px[10] == px[11];
                if (m.version == 4 && pixelsDoubled) {
                    b.mode = 1;
                    b.data = {{kFull, static_cast<std::uint32_t>(px[0] | px[2] << 8)},
                              {kFull, static_cast<std::uint32_t>(px[8] | px[10] << 8)}};
                } else if (m.version == 4 && rowsDoubled) {
                    b.mode = 2;
                    b.data = {
                        {kFull, pair(0, 2)}, {kFull, pair(0, 0)}, {kFull, pair(2, 2)}, {kFull, pair(2, 0)}};
                } else {
                    for (int y = 0; y < 4; ++y) {
                        b.data.push_back({kFull, pair(y, 2)});
                        b.data.push_back({kFull, pair(y, 0)});
                    }
                }
            }
            blocks.push_back(std::move(b));
        }
    }
    // Group consecutive blocks of the same kind (same colour/mode) into runs.
    std::vector<Run> runs;
    std::size_t i = 0;
    while (i < blocks.size()) {
        std::size_t n = 1;
        while (i + n < blocks.size() && blocks[i + n].kind == blocks[i].kind &&
               blocks[i + n].color == blocks[i].color && blocks[i + n].mode == blocks[i].mode)
            ++n;
        std::size_t code = 63;
        while (kRuns[code] > n)
            --code;
        const std::size_t len = kRuns[code];
        Run r{static_cast<std::uint32_t>(blocks[i].kind) | static_cast<std::uint32_t>(code) << 2 |
                  (blocks[i].kind == 3 ? blocks[i].color << 8 : 0),
              {}};
        if (blocks[i].kind == 1 && m.version == 4) {
            if (blocks[i].mode == 1) {
                r.data.push_back({-1, 1});
            } else {
                r.data.push_back({-1, 0});
                r.data.push_back({-1, blocks[i].mode == 2 ? 1u : 0u});
            }
        }
        for (std::size_t k = 0; k < len; ++k)
            r.data.insert(r.data.end(), blocks[i + k].data.begin(), blocks[i + k].data.end());
        runs.push_back(std::move(r));
        i += len;
    }
    return runs;
}

inline std::vector<std::uint8_t> encode(const Movie& m) {
    const std::size_t nf = m.frames.size();
    // Video runs of every frame first: the trees cover the values they use.
    std::vector<std::vector<Run>> video;
    std::array<std::set<std::uint32_t>, 4> used;
    std::vector<std::uint8_t> prev;
    for (const auto& f : m.frames) {
        video.push_back(encodeVideo(m, prev, f.pixels));
        for (const auto& r : video.back()) {
            used[kType].insert(r.type);
            for (const auto& [t, v] : r.data)
                if (t >= 0)
                    used[static_cast<std::size_t>(t)].insert(v);
        }
        prev = f.pixels;
    }
    BitWriter tw;
    std::array<BigCodes, 4> trees;
    for (int t = 0; t < 4; ++t)
        trees[static_cast<std::size_t>(t)] = writeBigTree(tw, used[static_cast<std::size_t>(t)]);
    const auto treeBytes = tw.take();

    std::array<std::uint32_t, 7> maxAudio{};
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<std::uint8_t> types;
    std::array<std::uint8_t, 768> pal{};
    for (std::size_t i = 0; i < nf; ++i) {
        const auto& f = m.frames[i];
        std::vector<std::uint8_t> d;
        std::uint8_t type = 0;
        if (f.palette) {
            const auto chunk = encodePalette(pal, *f.palette);
            d.insert(d.end(), chunk.begin(), chunk.end());
            pal = *f.palette;
            type |= 1;
        }
        for (std::size_t t = 0; t < 7; ++t) {
            if (!m.tracks[t] || f.audio[t].empty())
                continue;
            const auto a = encodeAudio(*m.tracks[t], f.audio[t]);
            putLE32(d, static_cast<std::uint32_t>(a.size() + 4));
            d.insert(d.end(), a.begin(), a.end());
            maxAudio[t] = std::max(
                maxAudio[t], static_cast<std::uint32_t>(f.audio[t].size() * (m.tracks[t]->is16 ? 2 : 1)));
            type |= static_cast<std::uint8_t>(2u << t);
        }
        BitWriter vw;
        for (const auto& r : video[i]) {
            trees[kType].put(vw, r.type);
            for (const auto& [t, v] : r.data) {
                if (t < 0)
                    vw.bit(v);
                else
                    trees[static_cast<std::size_t>(t)].put(vw, v);
            }
        }
        const auto vb = vw.take();
        d.insert(d.end(), vb.begin(), vb.end());
        while (d.size() % 4)
            d.push_back(0);
        frames.push_back(std::move(d));
        types.push_back(type);
    }

    std::vector<std::uint8_t> out;
    const char* sig = m.version == 4 ? "SMK4" : "SMK2";
    out.insert(out.end(), sig, sig + 4);
    putLE32(out, m.width);
    putLE32(out, m.height);
    putLE32(out, static_cast<std::uint32_t>(nf - ((m.flags & 1) ? 1 : 0)));
    putLE32(out, static_cast<std::uint32_t>(m.rate));
    putLE32(out, m.flags);
    for (auto a : maxAudio)
        putLE32(out, a);
    putLE32(out, static_cast<std::uint32_t>(treeBytes.size()));
    for (const auto& t : trees)
        putLE32(out, t.allocSize);
    for (std::size_t t = 0; t < 7; ++t) {
        std::uint32_t r = 0;
        if (const auto& a = m.tracks[t]) {
            r = a->rate | 0x40000000u;
            if (a->compressed)
                r |= 0x80000000u;
            if (a->is16)
                r |= 0x20000000u;
            if (a->stereo)
                r |= 0x10000000u;
        }
        putLE32(out, r);
    }
    putLE32(out, 0);
    for (std::size_t i = 0; i < nf; ++i)
        putLE32(out, static_cast<std::uint32_t>(frames[i].size()) | (m.frames[i].keyframe ? 1u : 0u));
    out.insert(out.end(), types.begin(), types.end());
    out.insert(out.end(), treeBytes.begin(), treeBytes.end());
    for (const auto& f : frames)
        out.insert(out.end(), f.begin(), f.end());
    return out;
}

} // namespace brc::test::smk
