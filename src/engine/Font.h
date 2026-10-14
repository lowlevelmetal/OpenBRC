#pragma once

#include "engine/Element.h"
#include "engine/Image8.h"
#include "formats/Canvas.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {

class Resources;

// OMT 1.x canvas font (OMediaCanvasFont, 0x469030): glyphs in a canvas strip
// one cell wide, one cell per character. See docs/formats/font.md.
class Font {
public:
    struct Options {
        int cellWidth = 0;
        int cellHeight = 0;
        int verticalDelta = -1; // distance between cell tops; -1 = cellHeight
        bool proportional = true;
        int spaceSize = -1;     // -1 = cellWidth (setFontCanvas default)
        int charSpace = 2;      // gap after each glyph
    };

    // Loads the canvas `name` and converts it to the display palette (black
    // glyphs anti-aliased on a white key). Proportional widths are measured on
    // the canvas before conversion (0x469770, background = white).
    static std::shared_ptr<Font> load(Resources& resources, std::string_view name, const Options& options,
                                      const formats::Palette& display);
    // The game's fonts with the parameters their users set (font.md §3).
    static std::shared_ptr<Font> bureau18(Resources& r, const formats::Palette& display, bool bulletinBoard = false);
    static std::shared_ptr<Font> bureau10(Resources& r, const formats::Palette& display);

    int textWidth(std::string_view text) const;  // 0x469950
    int height() const { return m_cellH; }
    // drawString (0x469160): (x, y) is the top-left of the text.
    void draw(Image8& dst, std::string_view text, int x, int y, const Rect& clip) const;

private:
    int cell(unsigned char c) const;

    Image8 m_glyphs;
    int m_cellW = 0, m_cellH = 0, m_delta = 0;
    bool m_proportional = true;
    int m_space = 0, m_charSpace = 2;
    std::array<int, 256> m_width{};
};

// A line of text as an element (the OMT text element the game builds its name
// fields from): re-rendered when the text changes.
class TextElement final : public Element {
public:
    explicit TextElement(std::shared_ptr<const Font> font) : m_font(std::move(font)) {}

    void setText(std::string_view text);
    const std::string& text() const { return m_text; }
    const Font& font() const { return *m_font; }
    const Image8* image() const override { return m_text.empty() ? nullptr : &m_image; }

private:
    std::shared_ptr<const Font> m_font;
    std::string m_text;
    Image8 m_image;
};

} // namespace brc::engine
