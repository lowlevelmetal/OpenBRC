#include "app/Autoplay.h"

#include "core/Log.h"
#include "core/Png.h"
#include "core/StringUtil.h"

#include <charconv>
#include <format>

namespace brc::app {
namespace {

// The engine key code of a Windows virtual key (the keys the game handles).
int engineKey(int vk) {
    switch (vk) {
    case 0x1B: return engine::key::kEscape;
    case 0x08: return engine::key::kBackspace;
    case 0x0D: return engine::key::kEnter;
    case 0x2E: return engine::key::kDelete;
    case 0x25: return engine::key::kLeft;
    case 0x27: return engine::key::kRight;
    case 0x26: return engine::key::kUp;
    case 0x28: return engine::key::kDown;
    case 0x09: return engine::key::kTab;
    default: return 0;
    }
}

} // namespace

std::optional<Autoplay> Autoplay::parse(std::string_view script, std::filesystem::path snapDir, std::string* error) {
    Autoplay a;
    a.m_dir = std::move(snapDir);
    for (auto part : str::split(script, ';')) {
        part = str::trim(part);
        if (part.empty())
            continue;
        Step s;
        if (part.front() == '+') {
            const auto sp = part.find(' ');
            const auto num = part.substr(1, sp == std::string_view::npos ? part.size() - 1 : sp - 1);
            std::from_chars(num.data(), num.data() + num.size(), s.delay);
            part = sp == std::string_view::npos ? std::string_view{} : str::trim(part.substr(sp));
        }
        auto words = str::split(part, ' ');
        if (words.empty() || words[0].empty()) {
            if (error)
                *error = std::format("autoplay: empty step in '{}'", script);
            return std::nullopt;
        }
        s.cmd = std::string(words[0]);
        if (s.cmd == "type") {
            s.args.emplace_back(str::trim(part.substr(4)));
        } else {
            for (std::size_t i = 1; i < words.size(); ++i)
                if (!words[i].empty())
                    s.args.emplace_back(words[i]);
        }
        a.m_steps.push_back(std::move(s));
    }
    return a;
}

bool Autoplay::step(engine::Engine& app, std::uint32_t now, VirtualMouse& mouse) {
    if (!m_started) {
        m_started = true;
        m_last = now;
    }
    auto release = [&] {
        if (mouse.flushes == m_heldFlushes) {
            engine::InputEvent e;
            e.x = mouse.pos.first;
            e.y = mouse.pos.second;
            e.type = m_held == 1 ? engine::InputEvent::LeftUp : engine::InputEvent::RightUp;
            app.postInput(e);
        }
        if (m_held == 1)
            mouse.left = false;
        m_held = 0;
    };
    if (m_held && now >= m_releaseAt)
        release();
    while (m_next < m_steps.size()) {
        const Step& s = m_steps[m_next];
        if (now - m_last < s.delay)
            return true;
        auto num = [&](std::size_t i) {
            int v = 0;
            if (i < s.args.size())
                std::from_chars(s.args[i].data(), s.args[i].data() + s.args[i].size(), v);
            return v;
        };
        if (s.cmd == "wait") {
            const engine::Node* node = app.currentNode();
            if (!node || app.switchPending() || s.args.empty() || !str::iequals(node->name(), s.args[0]))
                return true;
        } else if (s.cmd == "click" || s.cmd == "rclick") {
            if (!m_moved) {
                mouse.pos = {num(0), num(1)};
                m_moved = true;
                return true;
            }
            m_moved = false;
            if (m_held)
                release();
            const bool left = s.cmd == "click";
            engine::InputEvent e;
            e.x = mouse.pos.first;
            e.y = mouse.pos.second;
            e.type = left ? engine::InputEvent::LeftDown : engine::InputEvent::RightDown;
            app.postInput(e);
            m_held = left ? 1 : 2;
            m_heldFlushes = mouse.flushes;
            if (left)
                mouse.left = true;
            const int hold = num(2);
            m_releaseAt = now + static_cast<std::uint32_t>(hold > 0 ? hold : 150);
        } else if (s.cmd == "move") {
            mouse.pos = {num(0), num(1)};
        } else if (s.cmd == "key") {
            engine::InputEvent e;
            e.key = num(0);
            e.type = engine::InputEvent::KeyDown;
            app.postInput(e);
            e.type = engine::InputEvent::KeyUp;
            app.postInput(e);
        } else if (s.cmd == "vkey") {
            // WM_KEYDOWN, WM_CHAR (when a character is given), WM_KEYUP.
            engine::InputEvent e;
            e.key = engineKey(num(0));
            e.type = engine::InputEvent::KeyDown;
            app.postInput(e);
            if (const int ch = num(1); ch > 0 && ch < 256) {
                engine::InputEvent c;
                c.type = engine::InputEvent::KeyDown;
                c.ch = static_cast<std::uint8_t>(ch);
                app.postInput(c);
            }
            e.type = engine::InputEvent::KeyUp;
            app.postInput(e);
        } else if (s.cmd == "type") {
            for (char c : s.args.empty() ? std::string{} : s.args[0]) {
                engine::InputEvent e;
                e.type = engine::InputEvent::KeyDown;
                e.ch = static_cast<std::uint8_t>(c);
                app.postInput(e);
            }
        } else if (s.cmd == "snap") {
            std::error_code ec;
            std::filesystem::create_directories(m_dir, ec);
            const auto path = m_dir / ((s.args.empty() ? std::string("snap") : s.args[0]) + ".png");
            const auto& scr = app.screen();
            png::write(path, png::encodeIndexed(scr.pixels, scr.width, scr.height, scr.width, app.screenPalette()));
            log::info("autoplay: snapshot {}", str::fromPath(path));
        } else if (s.cmd == "quit") {
            m_next = m_steps.size();
            return false;
        } else {
            log::warn("autoplay: unknown step '{}'", s.cmd);
        }
        ++m_next;
        m_last = now;
    }
    return true;
}

} // namespace brc::app
