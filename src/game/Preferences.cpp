// The options screen shown at every boot (Preferences, ctor 0x4216d0): music
// on/off, keyboard or mouse riding, Desktop Horse. See docs/engine/nodes.md
// "Preferences".
#include "core/File.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "game/Godiva.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <format>
#include <memory>
#include <span>

namespace brc::game {
namespace {

constexpr int kBoxId = 0x3E9; // box1..box6: 0x3E9..0x3EE
constexpr int kOkId = 0x3F1;
constexpr int kResetId = 0x3F2;
constexpr std::uint32_t kOkDelayMs = 200;

// Prefs/Preferences.omt (gPrefsDir 0x80a050 + gPrefsFile 0x80a060), here in
// the user data directory: 8 raw bytes, four little-endian int16.
std::filesystem::path prefsPath() { return paths::userDataDir() / "Prefs" / "Preferences.omt"; }

// PrefsCheckBox (name inferred, vtable 0x7f30dc): one "boxN" anim whose
// sequence is the checked state.
class PrefsCheckBox {
public:
    PrefsCheckBox(ToolbarNode& node, int index) : m_node(node), m_name(std::format("box{}", index + 1)) {
        m_anim = node.createAnimAtViewport(m_name);
        if (m_anim)
            m_anim->setClickId(kBoxId + index); // anim+0x11c
    }
    ~PrefsCheckBox() { m_node.engine().anims().remove(m_name); } // 0x421dc0

    bool checked() const { return m_checked; }
    void set(bool checked) {
        m_checked = checked;
        if (m_anim)
            m_anim->setSequence(checked ? 1 : 0, true);
    }
    void toggle() { set(!m_checked); }

private:
    ToolbarNode& m_node;
    std::string m_name;
    engine::Anim* m_anim = nullptr;
    bool m_checked = false; // +0x0c
};

class Preferences final : public ToolbarNode {
public:
    Preferences(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x4216d0.
        pause(true);
        engine().anims().preloadCache("preferences.omt.cache");
        setCursor("prefscursor");
        m_ok = button("okbtn", kOkId);
        m_reset = button("resetbtn", kResetId);
        for (int i = 0; i < 6; ++i)
            m_boxes[static_cast<std::size_t>(i)] = std::make_unique<PrefsCheckBox>(*this, i);
        // The row labels only roll over.
        m_controlsLabel = button("controlsbtn", 0);
        m_musicLabel = button("musicbtn", 0);
        m_horseLabel = button("horsebtn", 0);

        std::error_code ec;
        std::filesystem::create_directories(prefsPath().parent_path(), ec);
        const auto data = file::readBinary(prefsPath());
        if (!data || data->size() < sizeof m_prefs) {
            // No file: music on, mouse control, Desktop Horse on, and the intro voice.
            box(1).toggle();
            box(3).toggle();
            box(5).toggle();
            m_prefs[3] = 1;
        } else {
            std::memcpy(m_prefs.data(), data->data(), sizeof m_prefs);
            box(m_prefs[0] == 1 ? 0 : 1).toggle();
            box(m_prefs[1] == 0 ? 2 : 3).toggle();
            box(m_prefs[2] == 0 ? 4 : 5).toggle();
        }
        pause(false);
    }

    ~Preferences() override {
        // 0x421e20.
        m_controlsLabel.reset();
        m_musicLabel.reset();
        m_horseLabel.reset();
        m_ok.reset();
        m_reset.reset();
        for (auto& b : m_boxes)
            b.reset();
        auto& anims = engine().anims();
        for (std::string_view n : {"splash", "prefscursor"})
            if (anims.find(n))
                anims.remove(n);
    }

    void start() override {
        // 0x422010 (the base Start is not called).
        if (m_prefs[3] != 0) {
            playVoice("db02048");
            playVoiceNoFlaps("db02049");
            playVoiceNoFlaps("db02045");
            playVoiceNoFlaps("db02046");
            m_prefs[3] = 0;
        }
    }

    void onPress(const engine::Pick& pick) override {
        // 0x4220c0.
        const int id = pick.element ? pick.element->clickId() : 0;
        if (id >= kBoxId && id < kBoxId + 6) {
            // Toggles the box and its row partner (a click on the checked box swaps too).
            const int i = id - kBoxId;
            box(i).toggle();
            box(i % 2 == 0 ? i + 1 : i - 1).toggle();
            click("click");
            return;
        }
        if (id == kOkId) {
            click("click");
            savePreferences();
            m_okPressed = true;
        } else if (id == kResetId) {
            for (int i = 0; i < 6; ++i)
                box(i).set(i % 2 == 1);
            click("trash2");
            toolbarMouseDown(pick);
        } else {
            toolbarMouseDown(pick);
        }
    }

    void onKeyDown(const engine::InputEvent& e) override {
        // 0x421ff0: only typed characters reach the toolbar (so Escape does nothing here).
        if (e.key == 0 && e.ch != 0)
            ToolbarNode::onKeyDown(e);
    }

    void tick() override {
        // 0x422060: "signin" 200 ms after OK (globals 0x80a068 / 0x80a06c).
        if (m_okPressed) {
            const std::uint32_t now = engine().ticks();
            if (m_okTime == 0)
                m_okTime = now;
            else if (now - m_okTime > kOkDelayMs)
                app().gotoNode("signin");
        }
        ToolbarNode::tick();
    }

private:
    std::unique_ptr<RolloverButton> button(std::string_view anim, int clickId) {
        auto b = std::make_unique<RolloverButton>(*this, false);
        b->init(anim);
        if (clickId != 0 && b->anim())
            b->anim()->setClickId(clickId);
        return b;
    }
    PrefsCheckBox& box(int i) { return *m_boxes[static_cast<std::size_t>(i)]; }
    void click(std::string_view sound) {
        engine().sounds().playSound(sound, -1, false, false, 1, false, 0x70, -1, 0, false);
    }

    void savePreferences() {
        // 0x422300: the boxes become the file values and the application fields.
        Session& s = app().session();
        m_prefs[0] = box(0).checked() ? 1 : 0; // box1 = music off
        s.musicOff = static_cast<std::uint16_t>(m_prefs[0]);
        const bool keyboard = box(2).checked(); // box3 = keyboard
        m_prefs[1] = keyboard ? 0 : 1;
        s.mouseControl = static_cast<std::uint16_t>(m_prefs[1]);
        app().controlMode = keyboard ? 1 : 0;
        m_prefs[2] = box(4).checked() ? 0 : 1; // box5 = no Desktop Horse
        s.desktopHorse = static_cast<std::uint16_t>(m_prefs[2]);
        std::error_code ec;
        std::filesystem::create_directories(prefsPath().parent_path(), ec);
        if (!file::writeAtomic(prefsPath(), std::as_bytes(std::span(m_prefs))))
            log::warn("Preferences: cannot write {}", str::fromPath(prefsPath()));
    }

    std::unique_ptr<RolloverButton> m_ok, m_reset;                         // +0x1C4 / +0x1C8
    std::unique_ptr<RolloverButton> m_musicLabel, m_controlsLabel, m_horseLabel; // +0x1D8 / +0x1DC / +0x1E0
    std::array<std::unique_ptr<PrefsCheckBox>, 6> m_boxes;                // +0x1E4..+0x1F8
    // +0x1CE: music off, mouse control, Desktop Horse, intro voice pending.
    std::array<std::int16_t, 4> m_prefs{};
    bool m_okPressed = false;
    std::uint32_t m_okTime = 0;
};

BRC_REGISTER_NODE("preferences", [](Godiva& app, const std::string& name) {
    return std::make_unique<Preferences>(app, name);
});

} // namespace
} // namespace brc::game
