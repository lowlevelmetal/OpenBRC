#include "engine/Font.h"

#include "core/Log.h"
#include "engine/Resources.h"

#include <algorithm>

namespace brc::engine {

std::shared_ptr<Font> Font::load(Resources& resources, std::string_view name, const Options& o,
                                 const formats::Palette& display) {
    auto cv = resources.canvas(name);
    if (!cv) {
        log::warn("font: no canvas {}", name);
        return nullptr;
    }
    auto f = std::make_shared<Font>();
    f->m_cellW = o.cellWidth;
    f->m_cellH = o.cellHeight;
    f->m_delta = o.verticalDelta < 0 ? o.cellHeight : o.verticalDelta;
    f->m_proportional = o.proportional;
    f->m_space = o.spaceSize < 0 ? o.cellWidth : o.spaceSize;
    f->m_charSpace = o.charSpace;

    // Pixels: colour of each canvas pixel, and whether it is background.
    const int w = cv->width, h = cv->height;
    std::vector<std::uint32_t> rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    std::vector<std::uint8_t> background(rgb.size());
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        if (cv->depth > 8) {
            rgb[i] = cv->rgb[i];
            background[i] = cv->hasKey ? cv->rgb[i] == cv->key : (cv->rgb[i] & 0xFFFFFF) == 0xFFFFFF;
        } else if (cv->depth == 1) {
            rgb[i] = cv->indices[i] ? 0x000000 : 0xFFFFFF;
            background[i] = cv->indices[i] == 0;
        } else {
            rgb[i] = cv->palette ? cv->palette->rgb[cv->indices[i]] : 0;
            background[i] = cv->hasKey && cv->indices[i] == cv->key;
        }
    }
    f->m_glyphs = Image8(w, h, 0);
    f->m_glyphs.opaque.resize(rgb.size());
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        f->m_glyphs.pixels[i] = display.lookup(rgb[i]);
        f->m_glyphs.opaque[i] = background[i] ? 0 : 1;
    }

    // createProportionalTable (0x469770).
    f->m_width.fill(f->m_space);
    const int maxW = std::min(w, f->m_cellW);
    for (int top = 0; f->m_delta > 0 && top + f->m_cellH <= h && top / f->m_delta < 256; top += f->m_delta) {
        int widest = -1;
        for (int row = 0; row < f->m_cellH; ++row)
            for (int x = 0; x < maxW; ++x)
                if (!background[static_cast<std::size_t>(top + row) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)])
                    widest = std::max(widest, x);
        f->m_width[static_cast<std::size_t>(top / f->m_delta)] = widest >= 0 ? widest + 1 : f->m_space;
    }
    return f;
}

std::shared_ptr<Font> Font::bureau18(Resources& r, const formats::Palette& display, bool bulletinBoard) {
    return load(r, bulletinBoard ? "BureauGrotesque-FiveThree_18bb" : "BureauGrotesque-FiveThree_18",
                {37, 29, -1, true, 12, 1}, display);
}

std::shared_ptr<Font> Font::bureau10(Resources& r, const formats::Palette& display) {
    return load(r, "BureauGrotesque-FiveThree_10", {20, 17, -1, true, 6, 1}, display);
}

int Font::cell(unsigned char c) const {
    // trans[c] = 0 for c < 0x20, else c - 31 (the game's tables).
    return c < 0x20 ? 0 : c - 31;
}

int Font::textWidth(std::string_view text) const {
    // 0x469950: charSpace after every non-space character except the last.
    if (!m_proportional)
        return m_cellW * static_cast<int>(text.size());
    int w = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == ' ') {
            w += m_space;
        } else {
            w += m_width[static_cast<std::size_t>(cell(c))];
            if (i + 1 < text.size())
                w += m_charSpace;
        }
    }
    return w;
}

void Font::draw(Image8& dst, std::string_view text, int x, int y, const Rect& clip) const {
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (m_proportional && c == ' ') {
            x += m_space;
            continue;
        }
        const int cl = cell(c);
        const int gw = m_proportional ? m_width[static_cast<std::size_t>(cl)] : m_cellW;
        const Rect src{0, cl * m_delta, gw, cl * m_delta + m_cellH};
        blit(dst, m_glyphs, src, x, y, clip);
        x += m_proportional ? gw + m_charSpace : m_cellW;
    }
}

void TextElement::setText(std::string_view text) {
    // Render on two different backgrounds: pixels that agree are glyph pixels.
    m_text = text;
    const int w = std::max(1, m_font->textWidth(m_text));
    Image8 a(w, m_font->height(), 1), b(w, m_font->height(), 2);
    m_font->draw(a, m_text, 0, 0, a.bounds());
    m_font->draw(b, m_text, 0, 0, b.bounds());
    m_image = Image8(w, m_font->height(), 0);
    m_image.opaque.assign(m_image.pixels.size(), 0);
    for (std::size_t i = 0; i < m_image.pixels.size(); ++i) {
        if (a.pixels[i] == b.pixels[i]) {
            m_image.opaque[i] = 1;
            m_image.pixels[i] = a.pixels[i];
        }
    }
    markChanged();
}

} // namespace brc::engine
