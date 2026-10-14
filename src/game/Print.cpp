// Printing (0x422600–0x422cff): pages are saved as PNG files instead of being
// sent to a printer. See game/Print.h.
#include "game/Print.h"

#include "core/Log.h"
#include "core/Paths.h"
#include "core/Png.h"
#include "core/StringUtil.h"
#include "engine/Element.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "game/Godiva.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>
#include <vector>

namespace brc::game::print {
namespace {

constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kMargin = 24;    // page margin around the picture *(not in the original: printer page layout)*
constexpr int kFooterGap = 8;  // picture to footer

// A 32-bit page (the original's 0x2AD canvases converted to 32 bpp).
struct Page {
    int width = 0, height = 0;
    std::vector<std::uint32_t> rgb; // 0x00RRGGBB

    Page(int w, int h) : width(w), height(h), rgb(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), kWhite) {}
    std::uint32_t& at(int x, int y) { return rgb[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)]; }
};

// Draws an 8-bit image with the display palette; transparent pixels are left
// alone (the bitmap conversion 0x44ee20 prints the magenta key as white).
// The image is stretched to w x h (COLORONCOLOR) when the sizes differ.
void drawImage(Page& page, const engine::Image8& img, int x0, int y0, int w, int h,
               const std::array<std::uint32_t, 256>& palette) {
    if (img.width <= 0 || img.height <= 0 || w <= 0 || h <= 0)
        return;
    for (int y = 0; y < h; ++y) {
        const int py = y0 + y;
        if (py < 0 || py >= page.height)
            continue;
        const int sy = y * img.height / h;
        for (int x = 0; x < w; ++x) {
            const int px = x0 + x;
            if (px < 0 || px >= page.width)
                continue;
            const int sx = x * img.width / w;
            if (img.transparentAt(sx, sy))
                continue;
            page.at(px, py) = palette[img.at(sx, sy)] & 0xFFFFFF;
        }
    }
}

void drawImage(Page& page, const engine::Image8& img, int x0, int y0, const std::array<std::uint32_t, 256>& palette) {
    drawImage(page, img, x0, y0, img.width, img.height, palette);
}

// The footer line (0x44e930's last argument): the copyright, 11 spaces and
// the player's name padded with spaces to 68 characters.
std::string footerText(Godiva& app) {
    std::string name = app.record().name();
    if (name.size() < 0x44)
        name.append(0x44 - name.size(), ' ');
    return std::string("\xA9 1998 Mattel, Inc.  All Rights Reserved           ") + name;
}

std::filesystem::path nextPrintoutPath() {
    const auto dir = paths::userDataDir() / "Printouts";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::string stem = std::format("Barbie Riding Club {:%Y-%m-%d %H%M%S}", now);
    auto path = dir / (stem + ".png");
    for (int n = 2; std::filesystem::exists(path, ec); ++n)
        path = dir / std::format("{} ({}).png", stem, n);
    return path;
}

// Lays the picture out on a white page with the footer under it and saves it
// (0x44e930: document "Barbie Riding Club", one page).
std::optional<std::filesystem::path> printPage(Godiva& app, const Page& picture) {
    const formats::Palette& display = app.gamePalette();
    const auto font = engine::Font::bureau10(app.resources(), display);
    // The padding at the end of the line is invisible: it does not widen the page.
    std::string footer = footerText(app);
    footer.erase(footer.find_last_not_of(' ') + 1);
    const int footerW = font ? font->textWidth(footer) : 0;
    const int footerH = font ? font->height() : 0;

    const int width = std::max(picture.width, footerW) + 2 * kMargin;
    const int height = picture.height + kFooterGap + footerH + 2 * kMargin;
    Page page(width, height);
    const int px = (width - picture.width) / 2;
    for (int y = 0; y < picture.height; ++y)
        for (int x = 0; x < picture.width; ++x)
            page.at(px + x, kMargin + y) = picture.rgb[static_cast<std::size_t>(y) * static_cast<std::size_t>(picture.width) + static_cast<std::size_t>(x)];
    if (font && footerW > 0) {
        const std::uint8_t white = display.lookup(kWhite);
        engine::Image8 line(footerW, footerH, white);
        font->draw(line, footer, 0, 0, line.bounds());
        drawImage(page, line, (width - footerW) / 2, kMargin + picture.height + kFooterGap, display.rgb);
    }

    std::vector<std::uint8_t> rgba(page.rgb.size() * 4);
    for (std::size_t i = 0; i < page.rgb.size(); ++i) {
        rgba[i * 4 + 0] = static_cast<std::uint8_t>(page.rgb[i] >> 16);
        rgba[i * 4 + 1] = static_cast<std::uint8_t>(page.rgb[i] >> 8);
        rgba[i * 4 + 2] = static_cast<std::uint8_t>(page.rgb[i]);
        rgba[i * 4 + 3] = 0xFF;
    }
    const auto path = nextPrintoutPath();
    if (!png::write(path, png::encodeRgba(rgba, page.width, page.height, page.width * 4))) {
        log::warn("Print: cannot write {}", str::fromPath(path));
        return std::nullopt;
    }
    log::info("Print: saved {}", str::fromPath(path));
    return path;
}

} // namespace

std::optional<std::filesystem::path> canvasCentered(Godiva& app, const engine::Image8& fg, const engine::Image8& bg) {
    // 0x422600: a copy of bg, fg stretched into the rectangle centred on it
    // (left = (bgW - fgW) / 2, right = bgW - left), converted to 32 bpp.
    const auto& pal = app.gamePalette().rgb;
    Page picture(bg.width, bg.height);
    drawImage(picture, bg, 0, 0, pal);
    const int left = (bg.width - fg.width) / 2, top = (bg.height - fg.height) / 2;
    drawImage(picture, fg, left, top, bg.width - 2 * left, bg.height - 2 * top, pal);
    return printPage(app, picture);
}

std::optional<std::filesystem::path> element(Godiva& app, const engine::Element& e) {
    // 0x422850: the element's canvas (vt+0x7c) converted to 32 bpp.
    const engine::Image8* img = e.image();
    if (!img)
        return std::nullopt;
    Page picture(img->width, img->height);
    drawImage(picture, *img, 0, 0, app.gamePalette().rgb);
    return printPage(app, picture);
}

std::optional<std::filesystem::path> elementWithText(Godiva& app, const engine::Element& e, const engine::Element& text,
                                                     int boxW, int boxH, int border) {
    // 0x4229d0: the text field is drawn into its own 32-bit world, filled
    // first (white here *(inferred: the fill colour comes from the text
    // style's 0xFF colours)*), with the text at the border; that world is
    // copied onto the picture at x = w/2 - boxW/2, y = trunc(field.y - element.y).
    const engine::Image8* img = e.image();
    if (!img)
        return std::nullopt;
    const auto& pal = app.gamePalette().rgb;
    Page picture(img->width, img->height);
    drawImage(picture, *img, 0, 0, pal);
    const int x = picture.width / 2 - boxW / 2;
    const int y = static_cast<int>(text.y() - static_cast<float>(border) - e.y());
    for (int by = 0; by < boxH; ++by)
        for (int bx = 0; bx < boxW; ++bx)
            if (x + bx >= 0 && x + bx < picture.width && y + by >= 0 && y + by < picture.height)
                picture.at(x + bx, y + by) = kWhite;
    if (const engine::Image8* t = text.image())
        drawImage(picture, *t, x + border, y + border, pal);
    return printPage(app, picture);
}

} // namespace brc::game::print
