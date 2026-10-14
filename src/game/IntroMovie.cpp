// The intro movie after sign-in (IntroMovie.cpp, ctor 0x41e990): smk\Intro.smk
// centred on a black screen, then the yard (no horse yet) or the tack room.
// The original also measured the machine with it (perf.inf, the low-end flag);
// see docs/engine/movies.md §2 and docs/formats/savegame.md §5.
#include "core/File.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "engine/Engine.h"
#include "engine/Movie.h"
#include "engine/SoundManager.h"
#include "game/Godiva.h"
#include "game/ToolbarNode.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace brc::game {
namespace {

std::filesystem::path perfPath() { return paths::userDataDir() / "perf.inf"; }

// perf.inf as the original's text-mode fread sees it: a 0x0D before 0x0A is
// dropped and 0x1A ends the file. Missing bytes stay zero.
std::array<float, 8> readPerf(const std::vector<std::byte>& raw) {
    std::vector<std::uint8_t> bytes;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const auto b = static_cast<std::uint8_t>(raw[i]);
        if (b == 0x1A)
            break;
        if (b == 0x0D && i + 1 < raw.size() && static_cast<std::uint8_t>(raw[i + 1]) == 0x0A)
            continue;
        bytes.push_back(b);
    }
    std::array<float, 8> f{};
    std::memcpy(f.data(), bytes.data(), std::min(bytes.size(), sizeof f));
    return f;
}

class IntroMovie final : public ToolbarNode {
public:
    IntroMovie(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x41e990. The backdrop helper it installs (vtable 0x7f11a0) overrides
        // the solid colour fill with an empty draw, so the background canvas
        // never shows.
        setBackground(nullptr);
        listenToMouseButtons();
        // The intro can be skipped only once perf.inf exists, that is after it
        // has been watched to the end once.
        std::error_code ec;
        m_skippable = std::filesystem::is_regular_file(perfPath(), ec);
        setUpdateMode(0);
        setFrameInterval(100);
    }

    ~IntroMovie() override {
        // 0x41ebb0: the performance test, then the movie goes.
        lowEndTest();
        if (m_movie) {
            m_movie->setPaused(true);
            m_movie->hide(true);
            m_movie.reset();
        }
    }

    void start() override {
        // 0x41f0d0.
        m_movie = std::make_unique<engine::HcMovie>(engine(), *this);
        if (!m_movie->open("smk\\Intro.smk", false, false, false, true, 0, true))
            log::warn("Smacker File Failed to Load!");
        m_movie->setPosition(64.0f, 48.0f, 9000.0f);
        m_movie->link(this);
        m_movie->setPaused(false);
    }

    void tick() override {
        // SpendTime (0x41ef90).
        if (m_movie && m_movie->done()) {
            engine().sounds().stopAll(true);
            leave();
        }
        ToolbarNode::tick();
    }

    void listen(int message, const engine::MessageData& data) override {
        // 0x41f010: a mouse button release skips; nothing reaches the toolbar
        // while the intro cannot be skipped.
        if (!m_skippable)
            return;
        if (message == -103)
            leave();
        ToolbarNode::listen(message, data);
    }

    void onKeyDown(const engine::InputEvent& e) override {
        // 0x41f080: any typed character skips.
        if (m_skippable && e.ch != 0)
            leave();
    }

private:
    void leave() { app().gotoNode(app().record().horse() == PlayerRecord::kNoHorse ? "yard" : "tackroom"); }

    void lowEndTest() {
        // A new perf.inf gets the values of the disc's template (the developers'
        // machine): decoding a 1998 movie measures nothing on a current
        // computer, so the machine is never low-end (docs/status.md). The
        // original's verdict is still logged; its thresholds are the registry
        // defaults LowEndFlag_PercentDecompression 30, LowEndFlag_SoundFramesToDrop 0.
        constexpr float kPercentDecompression = 30.0f, kSoundFramesToDrop = 0.0f;
        std::array<float, 8> f{};
        if (auto raw = file::readBinary(perfPath())) {
            f = readPerf(*raw);
        } else {
            f = {0.0879f, 0.0f, 0.0016f, 8175260.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            if (!file::writeAtomic(perfPath(), std::as_bytes(std::span(f))))
                log::warn("IntroMovie: cannot write {}", str::fromPath(perfPath()));
        }
        log::info("Performance Summary - Decompression percent = {}, Drawing percent = {}, Reading percent = {}, "
                  "Read Speed = {}, Sound Skips = {}",
                  f[0], f[1], f[2], f[3], f[4]);
        const bool measured = f[0] * 100.0f > kPercentDecompression || f[4] > kSoundFramesToDrop;
        log::info("LowEnd Machine - 0 (the file says {})", measured ? 1 : 0);
        app().session().lowEnd = 0;
    }

    bool m_skippable = false;                // +0x1C0
    std::unique_ptr<engine::HcMovie> m_movie; // +0x1C4
};

BRC_REGISTER_NODE("intromovie", [](Godiva& app, const std::string& name) {
    return std::make_unique<IntroMovie>(app, name);
});

} // namespace
} // namespace brc::game
