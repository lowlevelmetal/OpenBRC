#pragma once

// The bulletin board scene (bulletinboard.cpp, 0x40bb90–0x40ec3f) and the
// panels it opens; the handbook family lives in Handbook.cpp. See
// docs/engine/classes.md ("BBPanel", "Handbook") and docs/engine/nodes.md.

#include "game/ToolbarNode.h"

#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {
class Anim;
class Element;
class TextElement;
} // namespace brc::engine

namespace brc::game {

class ACButton;
class BulletinBoard;

// Click ids shared by the panels (anim+0x11c).
namespace bb {
inline constexpr int kNext = 0x3474;  // right page / right arrow
inline constexpr int kPrev = 0x3475;  // left page / left arrow
inline constexpr int kGo = 0x3478;    // "Go!" (clue book, certificates book)
inline constexpr int kPage = 0x3479;  // the page itself
} // namespace bb

// The forced show used all over the board ("if hidden: counter = 1, Show").
void forceShow(engine::Element* e);

// BBPanel (name inferred, vtable 0x7f0c18): a popup of the board on one anim
// (+8). The board shows one panel at a time (node+0x1D8) and forwards clicks
// and sound ends to it while it is visible.
class BBPanel {
public:
    // The anim comes from CreateAnimAtViewport (0x41d100) in each constructor.
    BBPanel(BulletinBoard& node, std::string_view anim, std::string_view instance = {});
    virtual ~BBPanel(); // 0x40bd70: removes the anim
    BBPanel(const BBPanel&) = delete;
    BBPanel& operator=(const BBPanel&) = delete;

    virtual void soundEnded(const std::string& name) { (void)name; } // +0x04 (0x40bd60: nothing)
    virtual void hide();                               // +0x0C (0x40bda0)
    virtual void show();                               // +0x10 (0x40bdb0)
    virtual bool isVisible() const;                    // +0x14 (0x40bdc0)
    virtual void mouseDown(const engine::Pick& pick);  // +0x18 (0x40bde0): the toolbar's

protected:
    BulletinBoard& m_node; // +0x04
    engine::Anim* m_anim;  // +0x08
};

// BulletinBoard (ctor 0x40def0, vtable 0x7f0df8): the club's board in the
// tack room. Its buttons open the handbook, the certificates book, the clue
// book, the membership card and the team photo; a click at the left or right
// screen edge closes the open panel or goes back to the tack room.
class BulletinBoard final : public ToolbarNode {
public:
    BulletinBoard(Godiva& app, const std::string& name);
    ~BulletinBoard() override; // 0x40e970

    // vt+0xDC (0x40e240): hides the open panel, shows `panel`.
    void openPanel(BBPanel* panel);
    // HCToolbarNode::MouseDown for the panels' unhandled clicks.
    void toolbarClick(const engine::Pick& pick) { toolbarMouseDown(pick); }
    // +0x1C0: the handbook's print dialog (created by the handbook).
    std::unique_ptr<BBPanel>& printDialog() { return m_printDialog; }
    // A pending "go ride to Rabbit Run first" voice ([0x809ad8], DB9011).
    static bool rabbitPending;

    bool returnToHandbook = false; // +0x1D4: closing the open panel reopens the handbook
    bool swallowMouseUp = false;   // +0x1D6: the next button-up is not an edge click

    void showExitDialog(bool save) override;                 // +0xD0 (0x40e290)
    void start() override;                                   // 0x40e2d0
    void onPress(const engine::Pick& pick) override;         // 0x40e410
    void soundDone(const std::string& name, const void* sound, bool completed) override; // 0x40e730
    void listen(int message, const engine::MessageData& data) override; // 0x40e7c0
    void leaving(const std::string& nextNode) override;      // 0x40eb60

private:
    template <class T>
    BBPanel* open(std::unique_ptr<BBPanel>& slot);

    std::unique_ptr<BBPanel> m_printDialog;  // +0x1C0
    std::unique_ptr<BBPanel> m_handbook;     // +0x1C4
    std::unique_ptr<BBPanel> m_clueBook;     // +0x1C8
    std::unique_ptr<BBPanel> m_certificates; // +0x1CC
    std::unique_ptr<BBPanel> m_membership;   // +0x1D0
    BBPanel* m_active = nullptr;             // +0x1D8
    std::unique_ptr<BBPanel> m_teamPhoto;    // +0x1E0
    std::unique_ptr<ACButton> m_handbookBtn, m_certificateBtn, m_clueBtn, m_riderBtn; // +0x1E4..+0x1F0
    std::unique_ptr<ACButton> m_photoBtn; // +0x1F4
};

// The player's name as an OMT string field (BBTextStyle + BBNameField, names
// inferred, 0x40cce0–0x40cef0, base 0x465d10): the bulletin-board 18-point
// font, linked to the node. The field is 40 pixels high and auto-sized
// (0x466d40) to the text plus the cursor margin and two borders, at most 300
// pixels; the text is drawn inside the border. The returned element is the
// text itself: place it with placeNameField.
inline constexpr int kNameBorder = 3;   // +0x100
inline constexpr int kNameMargin = 5;   // +0x13C
inline constexpr int kNameHeight = 40;  // +0xFE
inline constexpr int kNameMaxWidth = 300; // +0x13A
std::unique_ptr<engine::TextElement> makeNameField(BulletinBoard& node);
int nameFieldWidth(const engine::TextElement& text);
// Puts the field's top-left at (x, y): the text goes to (x + 3, y + 3).
void placeNameField(engine::TextElement& text, float x, float y, float z);

// Handbook.cpp.
std::unique_ptr<BBPanel> makeHandbook(BulletinBoard& node); // 0x417d40

} // namespace brc::game
