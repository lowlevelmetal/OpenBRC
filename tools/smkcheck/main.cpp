// smkcheck: decodes every frame of Smacker streams (standalone .smk files and
// the SMK2/SMAK chunks of .omt containers) and reports frames, errors and
// timing. Can print per-frame checksums, dump frames as PNG or BMP, write a
// track's audio as WAV, test random access, and parse WAVE chunks.
#include "core/File.h"
#include "core/Log.h"
#include "core/Png.h"
#include "core/StringUtil.h"
#include "formats/OmtFile.h"
#include "formats/Smacker.h"
#include "formats/Wave.h"
#include "vfs/GameSource.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <print>
#include <string>
#include <vector>

namespace brc {
namespace {

namespace fs = std::filesystem;
using formats::Smacker;

constexpr const char* kUsage = R"(usage: smkcheck [options] <input>...

Inputs are host paths: .smk files, .omt/.omt.cache containers (every SMK2 and
SMAK chunk is checked) or directories (searched recursively). Select one chunk
of a container with "file.omt#SMAK:12". With --source, inputs are paths in the
game data ("resources/smk/intro.smk"), and --all checks every file there.

options:
  --source <path>     read the game data from a disc image, disc or install
  --all               every .smk and .omt in the game data (with --source)
  -q                  print only problems and the summary
  --checksums         print a CRC-32 of every frame's pixels, palette and audio
  --seek              after decoding, test gotoFrame() against sequential frames
  --dump <n|all>      write frame n (0-based) or every frame
  --out <path>        file (.png or .bmp) for --dump n; directory for --dump all
  --wav <path>        write the audio of --track (default 0) of the first stream
  --track <n>         audio track for --wav
  --waves             parse the WAVE chunks of containers instead of Smacker
)";

struct Options {
    std::vector<std::string> sources;
    bool all = false;
    bool quiet = false;
    bool checksums = false;
    bool seek = false;
    bool waves = false;
    std::optional<std::string> dump;
    std::string out;
    std::string wav;
    int track = 0;
};

struct Totals {
    std::size_t streams = 0, failedStreams = 0, frames = 0, errors = 0, audioSamples = 0;
    std::size_t seekChecks = 0, seekMismatches = 0;
    std::size_t waves = 0, waveErrors = 0, waveFrames = 0;
    double seconds = 0, movieSeconds = 0;
    std::map<std::string, std::size_t> waveFormats;
};

std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (const auto b : data)
        crc = table[(crc ^ b) & 0xff] ^ (crc >> 8);
    return ~crc;
}

std::uint32_t crc32(std::span<const std::int16_t> pcm) {
    // Little-endian bytes, as in a WAV file.
    std::vector<std::uint8_t> bytes(pcm.size() * 2);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        bytes[i * 2] = static_cast<std::uint8_t>(pcm[i]);
        bytes[i * 2 + 1] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(pcm[i]) >> 8);
    }
    return crc32(bytes);
}

void put16(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
}
void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    put16(v, x & 0xffff);
    put16(v, x >> 16);
}

// Uncompressed 8-bit BMP with the frame's palette.
std::vector<std::uint8_t> encodeBmp(std::span<const std::uint8_t> pixels, std::uint32_t w, std::uint32_t h,
                                    const std::array<std::uint8_t, 768>& pal) {
    const std::uint32_t stride = (w + 3) & ~3u;
    const std::uint32_t offset = 14 + 40 + 1024;
    std::vector<std::uint8_t> v;
    v.reserve(offset + stride * h);
    v.push_back('B');
    v.push_back('M');
    put32(v, offset + stride * h);
    put32(v, 0);
    put32(v, offset);
    put32(v, 40);
    put32(v, w);
    put32(v, h);
    put16(v, 1);
    put16(v, 8);
    put32(v, 0);
    put32(v, stride * h);
    put32(v, 2835);
    put32(v, 2835);
    put32(v, 256);
    put32(v, 0);
    for (int i = 0; i < 256; ++i) {
        v.push_back(pal[static_cast<std::size_t>(i) * 3 + 2]);
        v.push_back(pal[static_cast<std::size_t>(i) * 3 + 1]);
        v.push_back(pal[static_cast<std::size_t>(i) * 3]);
        v.push_back(0);
    }
    for (std::uint32_t y = h; y-- > 0;) {
        const auto row = pixels.subspan(std::size_t{y} * w, w);
        v.insert(v.end(), row.begin(), row.end());
        v.resize(v.size() + (stride - w), 0);
    }
    return v;
}

std::vector<std::uint8_t> encodeWav(std::span<const std::int16_t> pcm, int channels, std::uint32_t rate) {
    std::vector<std::uint8_t> v;
    const auto bytes = static_cast<std::uint32_t>(pcm.size() * 2);
    v.insert(v.end(), {'R', 'I', 'F', 'F'});
    put32(v, 36 + bytes);
    v.insert(v.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    put32(v, 16);
    put16(v, 1);
    put16(v, static_cast<std::uint32_t>(channels));
    put32(v, rate);
    put32(v, rate * static_cast<std::uint32_t>(channels) * 2);
    put16(v, static_cast<std::uint32_t>(channels) * 2);
    put16(v, 16);
    v.insert(v.end(), {'d', 'a', 't', 'a'});
    put32(v, bytes);
    for (const auto s : pcm)
        put16(v, static_cast<std::uint16_t>(s));
    return v;
}

bool writeFile(const fs::path& path, std::span<const std::uint8_t> data) {
    return file::writeAtomic(path, std::as_bytes(data));
}

bool dumpFrame(const fs::path& path, const Smacker& smk) {
    const auto pixels = smk.frameBuffer();
    const auto& pal = smk.palette();
    if (str::iendsWith(str::fromPath(path), ".bmp"))
        return writeFile(path, encodeBmp(pixels, smk.width(), smk.height(), pal));
    std::array<std::uint32_t, 256> rgb{};
    for (std::size_t i = 0; i < 256; ++i)
        rgb[i] = std::uint32_t{pal[i * 3]} << 16 | std::uint32_t{pal[i * 3 + 1]} << 8 | pal[i * 3 + 2];
    const auto png = png::encodeIndexed(pixels, static_cast<int>(smk.width()), static_cast<int>(smk.height()),
                                        static_cast<int>(smk.width()), rgb);
    return png::write(path, png);
}

std::string safeName(std::string_view label) {
    std::string s(label);
    for (auto& c : s)
        if (c == '/' || c == '\\' || c == '#' || c == ':' || c == ' ')
            c = '_';
    return s;
}

void checkStream(const std::string& label, std::shared_ptr<const RandomAccessFile> file, const Options& opt,
                 Totals& totals, bool& wavWritten) {
    ++totals.streams;
    const auto t0 = std::chrono::steady_clock::now();
    auto opened = Smacker::open(std::move(file));
    if (!opened) {
        ++totals.failedStreams;
        ++totals.errors;
        std::println("{}: open failed: {}", label, opened.error());
        return;
    }
    Smacker& smk = *opened;
    smk.useOwnBuffer();
    std::size_t errors = 0, audioSamples = 0;
    std::vector<std::uint32_t> frameCrc(smk.frameCount());
    std::vector<std::int16_t> wavPcm;
    const bool wantWav = !opt.wav.empty() && !wavWritten && smk.audioTrack(opt.track).decodable();

    for (std::uint32_t f = 0; f < smk.frameCount(); ++f) {
        if (f > 0)
            smk.nextFrame();
        const auto r = smk.decodeFrame();
        if (!r) {
            if (errors < 5)
                std::println("{}: {}", label, r.error());
            ++errors;
        }
        frameCrc[f] = crc32(smk.frameBuffer());
        std::string audioCrc;
        for (int t = 0; t < formats::kSmackerMaxTracks; ++t) {
            const auto pcm = smk.audio(t);
            audioSamples += pcm.size();
            if (!pcm.empty() && opt.checksums)
                audioCrc += std::format(" a{}={:08x}/{}", t, crc32(pcm), pcm.size());
        }
        if (wantWav) {
            const auto pcm = smk.audio(opt.track);
            wavPcm.insert(wavPcm.end(), pcm.begin(), pcm.end());
        }
        if (opt.checksums) {
            const auto& pal = smk.palette();
            std::println("{} {:5} v={:08x} p={:08x}{}", label, f, frameCrc[f], crc32(pal), audioCrc);
        }
        if (opt.dump && (*opt.dump == "all" || str::parseInt(*opt.dump) == static_cast<long long>(f))) {
            fs::path path = str::toPath(opt.out);
            if (*opt.dump == "all")
                path /= std::format("{}_{:05}.png", safeName(label), f);
            else if (opt.out.empty())
                path = std::format("{}_{:05}.png", safeName(label), f);
            if (!dumpFrame(path, smk))
                std::println(stderr, "smkcheck: cannot write {}", str::fromPath(path));
        }
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (opt.seek && smk.frameCount() > 1) {
        // Deterministic pseudo-random seeks, plus wrapping past the end.
        std::uint32_t state = 12345;
        std::size_t mismatches = 0;
        for (int i = 0; i < 24; ++i) {
            state = state * 1103515245u + 12345u;
            const std::uint32_t target = (state >> 8) % smk.frameCount();
            smk.gotoFrame(target);
            (void)smk.decodeFrame();
            ++totals.seekChecks;
            if (crc32(smk.frameBuffer()) != frameCrc[target]) {
                if (mismatches == 0)
                    std::println("{}: gotoFrame({}) differs from sequential decoding", label, target);
                ++mismatches;
            }
        }
        smk.gotoFrame(smk.frameCount() - 1);
        (void)smk.decodeFrame();
        smk.nextFrame();
        (void)smk.decodeFrame();
        ++totals.seekChecks;
        if (crc32(smk.frameBuffer()) != frameCrc[0]) {
            std::println("{}: wrapping to frame 0 differs from the first frame", label);
            ++mismatches;
        }
        totals.seekMismatches += mismatches;
    }

    if (wantWav) {
        const auto& t = smk.audioTrack(opt.track);
        if (!writeFile(str::toPath(opt.wav), encodeWav(wavPcm, t.channels(), t.sampleRate)))
            std::println(stderr, "smkcheck: cannot write {}", opt.wav);
        wavWritten = true;
    }

    const double movie = static_cast<double>(smk.frameCount()) / smk.framesPerSecond();
    totals.frames += smk.frameCount();
    totals.errors += errors;
    totals.audioSamples += audioSamples;
    totals.seconds += seconds;
    totals.movieSeconds += movie;
    if (errors)
        ++totals.failedStreams;
    if (!opt.quiet || errors) {
        std::string audio;
        for (int t = 0; t < formats::kSmackerMaxTracks; ++t) {
            const auto& a = smk.audioTrack(t);
            if (a.present)
                audio += std::format(" a{}:{}Hz/{}bit/{}{}", t, a.sampleRate, a.is16Bit ? 16 : 8,
                                     a.stereo ? "stereo" : "mono", a.compressed ? "/huff" : "/pcm");
        }
        std::println("{}: SMK{} {}x{} {} frames{} {:.2f} fps flags={:#x}{} | {:.1f} ms, {:.3f} ms/frame{}",
                     label, smk.version(), smk.width(), smk.height(), smk.frameCount(),
                     smk.hasRingFrame() ? "+ring" : "", smk.framesPerSecond(), smk.flags(), audio,
                     seconds * 1000, seconds * 1000 / smk.frameCount(),
                     errors ? std::format(" | {} errors", errors) : "");
    }
}

void checkWaves(const formats::OmtFile& omt, const std::string& label, const Options& opt, Totals& totals) {
    for (const auto* c : omt.ofType(formats::chunk::kWave)) {
        ++totals.waves;
        const auto bytes = omt.read(*c);
        const auto w = formats::parseWave(bytes);
        const std::string name = std::format("{}#WAVE:{}", label, c->id);
        if (!w) {
            ++totals.waveErrors;
            std::println("{}: {}", name, w.error());
            continue;
        }
        totals.waveFrames += w->frames();
        ++totals.waveFormats[std::format("tag {} {} Hz {}-bit {}", w->formatTag, w->sampleRate, w->sourceBits,
                                         w->channels == 2 ? "stereo" : "mono")];
        if (!opt.quiet)
            std::println("{}: {} Hz {}-bit {} {} frames ({} ms)", name, w->sampleRate, w->sourceBits,
                         w->channels == 2 ? "stereo" : "mono", w->frames(), w->durationMs());
    }
}

bool isSmackerType(formats::FourCC t) {
    return t == formats::chunk::kSmacker2 || t == formats::chunk::kSmacker;
}

void checkInput(const std::string& label, std::shared_ptr<const RandomAccessFile> file,
                std::string_view select, const Options& opt, Totals& totals, bool& wavWritten) {
    std::array<std::byte, 4> magic{};
    file->readAt(0, magic);
    const bool smk = std::memcmp(magic.data(), "SMK", 3) == 0;
    if (smk) {
        if (!opt.waves)
            checkStream(label, std::move(file), opt, totals, wavWritten);
        return;
    }
    auto omt = formats::OmtFile::open(file, label);
    if (!omt) {
        ++totals.errors;
        std::println("{}: {}", label, omt.error());
        return;
    }
    if (opt.waves) {
        checkWaves(*omt, label, opt, totals);
        return;
    }
    for (const auto& c : omt->chunks()) {
        if (!isSmackerType(c.type))
            continue;
        const std::string name = std::format("{}#{}:{}", label, c.type.str(), c.id);
        if (!select.empty() && !str::iequals(select, std::format("{}:{}", c.type.str(), c.id)))
            continue;
        checkStream(name, omt->openChunk(c), opt, totals, wavWritten);
    }
}

bool wanted(std::string_view path) {
    return str::iendsWith(path, ".smk") || str::iendsWith(path, ".omt") || str::iendsWith(path, ".omt.cache");
}

} // namespace
} // namespace brc

int main(int argc, char** argv) {
    using namespace brc;
    log::setLevel(log::Level::Warn);
    Options opt;
    std::vector<std::string> inputs;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::println(stderr, "smkcheck: {} needs a value", a);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--source")
            opt.sources.push_back(next());
        else if (a == "--all")
            opt.all = true;
        else if (a == "-q")
            opt.quiet = true;
        else if (a == "--checksums")
            opt.checksums = true;
        else if (a == "--seek")
            opt.seek = true;
        else if (a == "--waves")
            opt.waves = true;
        else if (a == "--dump")
            opt.dump = next();
        else if (a == "--out")
            opt.out = next();
        else if (a == "--wav")
            opt.wav = next();
        else if (a == "--track")
            opt.track = static_cast<int>(str::parseInt(next()).value_or(0));
        else if (a == "-h" || a == "--help") {
            std::print("{}", kUsage);
            return 0;
        } else
            inputs.emplace_back(a);
    }
    if (inputs.empty() && !opt.all) {
        std::print(stderr, "{}", kUsage);
        return 2;
    }

    Totals totals;
    bool wavWritten = false;
    const auto start = std::chrono::steady_clock::now();
    auto splitSelect = [](const std::string& in) -> std::pair<std::string, std::string> {
        const auto hash = in.rfind('#');
        if (hash == std::string::npos)
            return {in, {}};
        return {in.substr(0, hash), in.substr(hash + 1)};
    };

    if (!opt.sources.empty()) {
        std::vector<vfs::GameSource> sources;
        for (const auto& s : opt.sources) {
            std::string error;
            auto src = vfs::probeGameSource(str::toPath(s), &error);
            if (!src) {
                std::println(stderr, "smkcheck: {}", error);
                return 2;
            }
            sources.push_back(std::move(*src));
        }
        const auto data = vfs::assembleGameData(std::move(sources));
        if (opt.all)
            for (const auto& e : data.vfs.listFiles())
                if (!e.isDirectory && wanted(e.path))
                    inputs.push_back(e.path);
        for (const auto& in : inputs) {
            const auto [path, select] = splitSelect(in);
            auto f = data.vfs.open(path);
            if (!f) {
                ++totals.errors;
                std::println("{}: not in the game data", path);
                continue;
            }
            checkInput(path, std::move(f), select, opt, totals, wavWritten);
        }
    } else {
        for (const auto& in : inputs) {
            const auto [path, select] = splitSelect(in);
            std::vector<fs::path> files;
            const fs::path p = str::toPath(path);
            std::error_code ec;
            if (fs::is_directory(p, ec)) {
                for (const auto& e : fs::recursive_directory_iterator(p, ec))
                    if (e.is_regular_file() && wanted(str::fromPath(e.path())))
                        files.push_back(e.path());
                std::ranges::sort(files);
            } else {
                files.push_back(p);
            }
            for (const auto& f : files) {
                auto file = OsFile::open(f);
                if (!file) {
                    ++totals.errors;
                    std::println("{}: cannot open", str::fromPath(f));
                    continue;
                }
                checkInput(str::fromPath(f), std::move(file), select, opt, totals, wavWritten);
            }
        }
    }

    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (opt.waves) {
        for (const auto& [fmt, n] : totals.waveFormats)
            std::println("  {:5} x {}", n, fmt);
        std::println("summary: {} WAVE chunks, {} errors, {} sample frames, {:.2f} s", totals.waves,
                     totals.waveErrors, totals.waveFrames, wall);
        return totals.waveErrors || totals.errors ? 1 : 0;
    }
    std::println("summary: {} streams ({} with errors), {} frames, {} errors, {} audio samples; decoded in "
                 "{:.2f} s ({:.0f}x real time, {:.3f} ms/frame)",
                 totals.streams, totals.failedStreams, totals.frames, totals.errors, totals.audioSamples,
                 totals.seconds, totals.seconds > 0 ? totals.movieSeconds / totals.seconds : 0.0,
                 totals.frames ? totals.seconds * 1000 / static_cast<double>(totals.frames) : 0.0);
    if (opt.seek)
        std::println("seek: {} checks, {} mismatches", totals.seekChecks, totals.seekMismatches);
    return totals.errors || totals.seekMismatches ? 1 : 0;
}
