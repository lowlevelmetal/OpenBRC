#pragma once

#include <filesystem>
#include <optional>

namespace brc::engine {
class Element;
struct Image8;
} // namespace brc::engine

namespace brc::game {

class Godiva;

// The print helpers (0x422600–0x422cff), used by the handbook and its print
// dialog. The original built a 32-bit bitmap and sent it to the Windows
// printer as the document "Barbie Riding Club" with the footer
// "© 1998 Mattel, Inc.  All Rights Reserved" + the player's name (0x44e930).
// Here the same page is saved as a PNG file in <user data>/Printouts (a
// deliberate difference, docs/status.md); the result is the file written.
namespace print {

// 0x422600 PrintCanvasCentered(fg, bg): `fg` drawn over the middle of `bg`
// (a photo in its big frame).
std::optional<std::filesystem::path> canvasCentered(Godiva& app, const engine::Image8& fg, const engine::Image8& bg);
// 0x422850 PrintElement: the element's current picture.
std::optional<std::filesystem::path> element(Godiva& app, const engine::Element& element);
// 0x4229d0 PrintElementOnWhite: the element's picture with a text field (the
// player's name, `text` drawn `border` pixels inside a boxW x boxH field)
// on white, centred horizontally, at the field's vertical offset from the
// element.
std::optional<std::filesystem::path> elementWithText(Godiva& app, const engine::Element& element,
                                                     const engine::Element& text, int boxW, int boxH, int border);

} // namespace print
} // namespace brc::game
