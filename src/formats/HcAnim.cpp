#include "formats/HcAnim.h"

#include "core/StringUtil.h"
#include "formats/ByteReader.h"

#include <charconv>
#include <format>

namespace brc::formats {
namespace {

using namespace anim_flags;

std::unexpected<std::string> fail(std::string_view what) {
    return std::unexpected(std::format("HCAn: {}", what));
}

// Frame sound names may carry a ".wav" extension, which the loader cuts off.
std::string stripWav(std::string name) {
    if (str::iendsWith(name, ".wav"))
        name.resize(name.size() - 4);
    return name;
}

std::expected<HcAnimDesc, std::string> parseV1(BeReader& r, int version) {
    // HC2DAnim::LoadV1 (0x448320): nSequences, then {first, last, unused}
    // per sequence; frame i shows still image "<name>_<i>". Fixed 15 fps.
    HcAnimDesc d;
    d.version = version;
    d.flags = kTimeBased;
    d.defaultFrameMs = 66; // 1000 / 15, truncated
    std::int32_t nseq = 0;
    if (!r.i32(nseq) || nseq < 0 || nseq > 1000)
        return fail("bad v1 sequence count");
    for (std::int32_t s = 0; s < nseq; ++s) {
        std::int32_t first = 0, last = 0, unused = 0;
        if (!r.i32(first) || !r.i32(last) || !r.i32(unused))
            return fail("truncated v1 sequence");
        if (first < 1 || last < first || last > 10000)
            return fail("bad v1 frame range");
        HcAnimSequence seq;
        for (std::int32_t i = first; i <= last; ++i) {
            if (static_cast<std::size_t>(i) > d.sourceOffsets.size())
                d.sourceOffsets.resize(static_cast<std::size_t>(i));
            HcAnimFrame f;
            f.source = static_cast<std::uint16_t>(i);
            f.durationMs = d.defaultFrameMs;
            seq.frames.push_back(std::move(f));
        }
        seq.frames.back().flags |= kFrameStop;
        d.sequences.push_back(std::move(seq));
    }
    return d;
}

} // namespace

std::expected<HcAnimDesc, std::string> parseHcAnim(std::span<const std::byte> data) {
    BeReader r(data);
    std::int32_t version = 0;
    if (!r.i32(version))
        return fail("truncated");
    if (version <= 1)
        return parseV1(r, version);
    if (version != 2)
        return fail(std::format("unsupported version {}", version));

    HcAnimDesc d;
    d.version = 2;
    if (!r.u16(d.defaultFrameMs) || !r.u16(d.defaultZ) || !r.u32(d.flags))
        return fail("truncated header");

    if (d.flags & kNoSources) {
        std::string ignored;
        if (!r.pascal(ignored))
            return fail("truncated header string");
    } else {
        std::uint16_t n = 0;
        if (!r.u16(n))
            return fail("truncated source table");
        d.sourceOffsets.resize(n);
        for (auto& o : d.sourceOffsets)
            if (!r.i16(o.x) || !r.i16(o.y))
                return fail("truncated source table");
    }

    std::uint16_t nseq = 0;
    if (!r.u16(nseq))
        return fail("truncated sequence count");
    d.sequences.resize(nseq);
    for (auto& seq : d.sequences) {
        std::uint16_t nframes = 0;
        if (!r.u16(nframes) || !r.u32(seq.flags))
            return fail("truncated sequence");
        seq.z = d.defaultZ;
        if ((seq.flags & kSeqHasZ) && !r.u16(seq.z))
            return fail("truncated sequence depth");
        seq.frames.resize(nframes);
        for (auto& f : seq.frames) {
            if (!r.u16(f.source) || !r.u32(f.flags))
                return fail("truncated frame");
            if (!d.sourceOffsets.empty() && (f.source < 1 || f.source > d.sourceOffsets.size()))
                return fail(std::format("frame source {} out of range", f.source));
            f.durationMs = d.defaultFrameMs;
            // Optional fields, in this order (0x4485b0).
            if ((f.flags & kFrameGoto) && !r.u16(f.gotoSequence))
                return fail("truncated goto");
            if ((f.flags & kFrameDuration) && !r.u16(f.durationMs))
                return fail("truncated duration");
            if (f.flags & kFrameSound) {
                if (!r.pascal(f.sound))
                    return fail("truncated sound name");
                f.sound = stripWav(std::move(f.sound));
            }
            if ((f.flags & kFrameSetSeq) && (!r.pascal(f.setSeqAnim) || !r.u16(f.setSeqIndex)))
                return fail("truncated set-sequence");
            if ((f.flags & kFramePauseAnim) && !r.pascal(f.pauseAnim))
                return fail("truncated pause name");
            if ((f.flags & kFrameStartScript) && !r.pascal(f.startScript))
                return fail("truncated script name");
            if ((f.flags & kFrameStopScript) && !r.pascal(f.stopScript))
                return fail("truncated script name");
        }
    }
    for (const auto& seq : d.sequences)
        for (const auto& f : seq.frames)
            if ((f.flags & kFrameGoto) && f.gotoSequence >= d.sequences.size())
                return fail(std::format("goto sequence {} out of range", f.gotoSequence));
    return d;
}

std::vector<SmkiEntry> parseSmki(std::span<const std::byte> data) {
    // 6 bytes per frame: u16 width, u16 height, i16 depth (always 8, ignored).
    std::vector<SmkiEntry> out;
    BeReader r(data);
    SmkiEntry e;
    std::int16_t depth = 0;
    while (r.u16(e.width) && r.u16(e.height) && r.i16(depth))
        out.push_back(e);
    return out;
}

std::expected<AnimCache, std::string> openAnimCache(std::shared_ptr<const RandomAccessFile> file, std::string label) {
    // "%-10d\r\n" container size, the container, then records of
    // [size "%-10d\r\n"][name padded to 32 "\r\n"]["SMK2\r\n" | "SMK1\r\n"][data].
    auto readLine = [&](std::uint64_t at, std::size_t n) -> std::optional<std::string> {
        std::string s(n, '\0');
        if (!file->readExact(at, std::as_writable_bytes(std::span(s))))
            return std::nullopt;
        return s;
    };
    auto parseSize = [](std::string_view s) -> std::optional<std::uint32_t> {
        s = str::trim(s);
        std::uint32_t v = 0;
        const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        if (ec != std::errc{} || p != s.data() + s.size())
            return std::nullopt;
        return v;
    };

    const auto head = readLine(0, 12);
    const auto containerSize = head ? parseSize(head->substr(0, 10)) : std::nullopt;
    if (!containerSize)
        return std::unexpected(std::format("{}: not an anim cache (no size prefix)", label));
    auto container = OmtFile::open(std::make_shared<SubFile>(file, 12, *containerSize), label);
    if (!container)
        return std::unexpected(container.error());

    AnimCache cache{std::move(*container), {}};
    const std::uint64_t end = file->size();
    std::uint64_t pos = 12 + std::uint64_t{*containerSize};
    while (pos + 0x34 <= end) {
        const auto hdr = readLine(pos, 0x34);
        if (!hdr)
            break;
        const auto size = parseSize(std::string_view(*hdr).substr(0, 10));
        if (!size)
            return std::unexpected(std::format("{}: bad record size at {}", label, pos));
        AnimCacheRecord rec;
        rec.name = std::string(str::trim(std::string_view(*hdr).substr(12, 32)));
        if (rec.name.size() > 31)
            rec.name.resize(31); // the game cuts names to 31 characters
        const std::string_view tag = std::string_view(*hdr).substr(46, 4);
        if (tag == "SMK1")
            rec.cropped = true;
        else if (tag != "SMK2")
            return std::unexpected(std::format("{}: unknown record type '{}' for {}", label, tag, rec.name));
        rec.offset = pos + 0x34;
        rec.size = *size;
        if (rec.offset + rec.size > end)
            return std::unexpected(std::format("{}: record {} runs past the end", label, rec.name));
        cache.records.push_back(std::move(rec));
        pos += 0x34 + *size;
    }
    return cache;
}

} // namespace brc::formats
