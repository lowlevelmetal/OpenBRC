// The sign-in screen (signin.cpp, class SignIn, ctor 0x427370): one card per
// saved game, a big card whose name field takes a new player's name, the
// New / Delete / Go / Help / Quit buttons and the trash confirmation. See
// docs/engine/nodes.md "SignIn".
#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Font.h"
#include "engine/SoundManager.h"
#include "game/Godiva.h"
#include "game/PlayerRecord.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <format>
#include <memory>
#include <optional>
#include <string>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kCardId = 1000; // + slot
constexpr int kQuitId = 2000;
constexpr int kGoId = 2001;
constexpr int kNewId = 2002;
constexpr int kHelpId = 2003;
constexpr int kDeleteId = 2004;
constexpr int kTrashYesId = 2006;
constexpr int kTrashNoId = 2007;
constexpr int kBigCardId = 2008;
// A press on a name field: the original reads the field's text pointer as the
// click id (MouseDown 0x428a00 takes element+0x11c), which matches no button.
constexpr int kFieldId = -1;

constexpr int kSlots = 10;
constexpr std::uint32_t kBlinkMs = 1000;

// [0x80a144]: the Go button's press count. A static of the original, so it
// outlives the node; only SpendTime resets it.
std::int16_t s_goCount = 0;

// The OMT 1.x string field (base ctor 0x465d10) as Godiva's name field
// (vtable 0x7f0ca8): a line of text with a 3-pixel border, auto-sized to the
// text, a bevelled block cursor that blinks while the field has the keyboard
// focus, key filter 3 (letters, digits, space) and a length limit. Godiva's
// draw override (0x40cde0) skips the background, so only the text and the
// cursor are drawn.
class NameField final : public engine::Element {
public:
    struct Colors {
        std::uint8_t light, face, dark; // cursor bevel *(colours read from the original's screen)*
    };

    NameField(engine::Engine& engine, std::shared_ptr<const engine::Font> font, Colors colors, int maxWidth)
        : m_engine(engine), m_font(std::move(font)), m_colors(colors), m_maxWidth(maxWidth) {
        setPerPixelHit(false); // the field's hit test is its rectangle (0x4680b0)
        setClickId(kFieldId);
        resize();
    }
    ~NameField() override {
        if (s_active == this)
            s_active = nullptr;
        if (s_keyFocus == this)
            s_keyFocus = nullptr;
    }

    // [0x80ea1c] the active field and [0x80a5a0]+0x24 the focused key listener.
    static inline NameField* s_active = nullptr;
    static inline NameField* s_keyFocus = nullptr;

    const std::string& text() const { return m_text; }
    int width() const { return m_width; }
    bool locked() const { return m_locked; }
    void setLocked(bool locked) { m_locked = locked; } // +0x148: a click does not focus it
    void setMaxLength(int n) { m_maxLength = n; }      // vt+0xbc (0x40ce90)

    // vt+0x98 SetText (0x466030).
    void setText(std::string_view text) {
        m_text = text;
        resize();
        markChanged();
    }

    // vt+0x9c Activate (0x4667b0): takes the keyboard focus, cursor on.
    void activate() {
        if (s_active == this)
            return;
        if (s_active)
            s_active->deactivate();
        m_cursorOn = true;
        m_blinkStart = -1;
        markChanged();
        s_keyFocus = this;
        s_active = this;
    }

    // vt+0xa0 Deactivate (0x466820).
    void deactivate() {
        if (s_active != this)
            return;
        s_keyFocus = nullptr;
        m_cursorOn = false;
        markChanged();
        s_active = nullptr;
    }

    // The pick side effect (0x466100): an unlocked field takes the focus.
    void clicked() {
        if (!m_locked)
            activate();
    }

    // Key events while the field holds the focus: Godiva's length limit
    // (0x40cea0), then the OMT handler (0x466370).
    void key(const engine::InputEvent& e) {
        if (!visible()) {
            deactivate();
            return;
        }
        if (s_active != this)
            return;
        const std::uint32_t now = m_engine.ticks();
        if (e.key == 0) {
            if (static_cast<int>(m_text.size()) == m_maxLength || !accepts(e.ch))
                return;
            const std::string longer = m_text + static_cast<char>(e.ch);
            if (m_font && m_font->textWidth(longer) > m_maxWidth - 2 * kBorder)
                return; // the character does not fit
            m_text = longer;
            restartBlink(now);
            resize();
        } else if (e.key == engine::key::kBackspace) {
            if (m_text.empty())
                return;
            m_text.pop_back();
            resize();
            restartBlink(now);
        } else if (e.key == engine::key::kEnter || e.key == 5) {
            deactivate();
        }
    }

    // A left click took the focus away (−109).
    void lostFocus() { deactivate(); }

    // The field's idle task (0x4666b0): a hidden field loses the focus; the
    // cursor blinks every 600 ms.
    void idle() {
        if (s_active != this)
            return;
        if (!visible()) {
            deactivate();
            return;
        }
        const std::uint32_t now = m_engine.ticks();
        if (m_blinkStart < 0) {
            m_blinkStart = now;
        } else if (static_cast<float>(now) - static_cast<float>(m_blinkStart) >= 600.0f) {
            m_blinkStart = now;
            m_cursorOn = !m_cursorOn;
            markChanged();
        }
    }

    const engine::Image8* image() const override { return &m_box; }

    void draw(engine::Image8& dst, float camX, float camY, const engine::Rect& viewRect) const override {
        // 0x466940: the text at the border, then the cursor after it.
        if (!visible() || !m_font)
            return;
        const engine::Rect r = screenRect(camX, camY).offset(viewRect.left, viewRect.top);
        const engine::Rect inner{r.left + kBorder, r.top + kBorder, r.right - kBorder, r.bottom - kBorder};
        m_font->draw(dst, m_text, inner.left, inner.top, inner.intersect(viewRect));
        if (!m_cursorOn)
            return;
        // 0x466880: kCursorWidth wide, inside the field less one pixel.
        int left = r.left + m_font->textWidth(m_text) + 1;
        if (!m_text.empty())
            left += kBorder;
        const engine::Rect box =
            engine::Rect{left, r.top + 1, left + kCursorWidth, r.bottom - 1}.intersect({r.left + 1, r.top + 1, r.right - 1, r.bottom - 1});
        if (box.empty())
            return;
        dst.fill(box.intersect(viewRect), m_colors.face);
        dst.fill(engine::Rect{box.left, box.top, box.right - 1, box.top + 1}.intersect(viewRect), m_colors.light);
        dst.fill(engine::Rect{box.left, box.top, box.left + 1, box.bottom - 1}.intersect(viewRect), m_colors.light);
        dst.fill(engine::Rect{box.right - 1, box.top, box.right, box.bottom}.intersect(viewRect), m_colors.dark);
        dst.fill(engine::Rect{box.left, box.bottom - 1, box.right, box.bottom}.intersect(viewRect), m_colors.dark);
    }

private:
    static constexpr int kBorder = 3;      // +0x100
    static constexpr int kCursorWidth = 5; // +0x13c
    static constexpr int kHeight = 40;     // +0xfe

    // Key filter 3 (0x466e00).
    static bool accepts(std::uint8_t ch) {
        if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == ' ')
            return true;
        // Accented letters in the Macintosh character set the filter was written for.
        for (const std::uint8_t c : {0x8E, 0x88, 0x8F, 0x9F, 0x8A, 0x9A, 0x95, 0x9D, 0x99, 0x90, 0x89, 0x94, 0x9E,
                                     0x93, 0x8B, 0x9B, 0x98, 0x8D})
            if (ch == c)
                return true;
        return false;
    }

    void restartBlink(std::uint32_t now) {
        m_blinkStart = now;
        m_cursorOn = true;
        markChanged();
    }

    // Auto-size (0x466d40): text, cursor and both borders, up to the maximum.
    void resize() {
        const int w = (m_font ? m_font->textWidth(m_text) : 0) + kCursorWidth + 2 * kBorder;
        m_width = std::min(w, m_maxWidth);
        m_box = engine::Image8(m_width, kHeight, 0);
        m_box.opaque.assign(m_box.pixels.size(), 0);
        markChanged();
    }

    engine::Engine& m_engine;
    std::shared_ptr<const engine::Font> m_font; // +0x124
    Colors m_colors;
    int m_maxWidth;                // +0x13a
    int m_maxLength = 0x7fff;      // +0x164
    std::string m_text;            // +0x11c
    int m_width = 0;               // +0xfc
    bool m_cursorOn = false;       // +0x128
    std::int64_t m_blinkStart = -1; // +0x12c (-1.0f: not started)
    bool m_locked = false;         // +0x148
    engine::Image8 m_box;          // the field's rectangle, for picking
};

// SignInCard (0x426ee0): a saved player's small card, the "smallcard" anim
// with the player's name in the 10-point font.
class SignInCard {
public:
    SignInCard(ToolbarNode& node, const std::string& name, int slot, std::shared_ptr<const engine::Font> font,
               NameField::Colors colors)
        : m_node(node) {
        m_anim = node.engine().anims().getOrCreate("smallcard", std::format("smcard{}", slot));
        if (m_anim)
            m_anim->setClickId(kCardId + slot);
        m_field = std::make_unique<NameField>(node.engine(), std::move(font), colors, 0x8c);
        m_field->link(&node);
        m_field->setText(name);
        m_field->setLocked(true);
    }
    ~SignInCard() {
        // 0x427130.
        if (m_anim)
            m_node.engine().anims().remove(m_anim->name());
    }

    engine::Anim* anim() const { return m_anim; }
    const std::string& text() const { return m_field->text(); } // 0x4271d0

    void hide() {
        // 0x427170(1).
        m_field->hide(true);
        if (m_anim)
            m_anim->hide(true);
    }

    void setPosition(float x, float y) {
        // 0x4272d0: the name centred 36 pixels below the card's top.
        if (!m_anim)
            return;
        m_anim->setPosition(x, y, m_anim->z());
        const int cardW = m_anim->image() ? m_anim->image()->width : 0;
        m_field->setPosition(static_cast<float>(cardW / 2) + x - static_cast<float>(m_field->width() / 2), y + 36.0f,
                             m_anim->z() + 1.0f);
    }

private:
    ToolbarNode& m_node;
    engine::Anim* m_anim = nullptr;    // +0x04
    std::unique_ptr<NameField> m_field; // +0x18
};

class SignIn final : public ToolbarNode {
public:
    SignIn(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x427370.
        engine().anims().preloadCache("signin.omt.cache");
        setSkin(kSign);
        setCursor("signincursor");
        m_quit = button("quitbtn", kQuitId);
        m_help = button("helpbtn", kHelpId);
        m_delete = button("deletebtn", kDeleteId);
        m_new = button("newfbtn", kNewId);
        m_go = button("homefbtn", kGoId);

        m_nameFont = engine::Font::bureau18(engine().resources(), palette());
        m_cardFont = engine::Font::bureau10(engine().resources(), palette());
        m_colors = {palette().lookup(0xFFFFFF), palette().lookup(0xEFEFE7), palette().lookup(0x6A736D)};

        // Slots 0-4 down the left, 5-9 down the middle, each step 30 right and 60 down.
        bool noSaves = true;
        const int midX = (viewRect().right - viewRect().left) / 2 - 20;
        for (int slot = 0; slot < kSlots; ++slot) {
            const bool exists = slotExists(slot);
            std::string playerName;
            if (exists) {
                if (auto rec = loadRecord(slot))
                    playerName = rec->name();
                noSaves = false;
            }
            auto& card = m_cards[static_cast<std::size_t>(slot)];
            card = std::make_unique<SignInCard>(*this, playerName, slot, m_cardFont, m_colors);
            if (!exists)
                card->hide();
            m_cardRollovers[static_cast<std::size_t>(slot)] = std::make_unique<ACRollover>(*this, card->anim(), 3);
            const int i = slot % 5;
            if (slot < 5)
                card->setPosition(static_cast<float>(20 + 30 * i), static_cast<float>(20 + 60 * i));
            else
                card->setPosition(static_cast<float>(midX + 30 * i), static_cast<float>(50 + 60 * i));
        }
        m_noSaves = noSaves;
        // Not in the original: the node hears the mouse broadcast (-102) after
        // a press has gone through it, to apply the key-focus rule of the event
        // translator (see onMouseDown).
        listenToMouseButtons();
    }

    ~SignIn() override {
        // 0x4287b0.
        closeTrash();
        for (auto& r : m_cardRollovers)
            r.reset();
        for (auto& c : m_cards)
            c.reset();
        m_field.reset();
        if (m_card)
            engine().anims().remove(m_card->name());
        m_quit.reset();
        m_help.reset();
        m_delete.reset();
        m_new.reset();
        m_go.reset();
        auto& anims = engine().anims();
        for (std::string_view n : {"exit_bgSign", "exit_yesSign", "exit_noSign", "exit_creditsSign", "signincard",
                                   "smallcard", "trash_bg", "trash_yes", "trash_no", "signincursor", "helpbtn",
                                   "quitbtn", "deletebtn", "newfbtn", "homefbtn"})
            if (anims.find(n))
                anims.remove(n);
    }

    void start() override {
        // 0x428300: the big card, its name field under the card's line.
        m_card = createAnimAtViewport("signincard");
        if (m_card)
            m_card->setClickId(kBigCardId);
        m_field = std::make_unique<NameField>(engine(), m_nameFont, m_colors, 0xb4);
        m_field->setMaxLength(12);
        m_field->link(this);
        if (m_card) {
            const engine::Rect r = m_card->rect();
            m_field->setPosition(static_cast<float>(r.left) + static_cast<float>(r.width() / 2),
                                 static_cast<float>(r.top) + 72.0f, m_card->z() + 1.0f);
        }
        if (!m_noSaves) {
            hideBigCard();
            playVoice("db03071");
            playVoiceAfterPause("db03072");
            return;
        }
        // No player yet: straight into name entry on the first free slot.
        m_entry = true;
        m_pending = firstFreeSlot();
        m_field->activate();
        startGoBlink();
        playVoice("db03073");
    }

    void onPress(const engine::Pick& pick) override {
        // MouseDown (0x428a00).
        if (auto* field = dynamic_cast<NameField*>(pick.element))
            field->clicked();
        const int id = pick.element ? pick.element->clickId() : 0;
        const int slot = id - kCardId;
        if (slot >= 0 && slot < kSlots) {
            // A saved player: show the name on the big card.
            if (s_goCount != 0)
                return;
            click("click");
            m_selected = m_pending = slot;
            m_field->setText(m_cards[static_cast<std::size_t>(slot)]->text());
            showBigCard();
            m_field->setLocked(true);
            startGoBlink();
            m_entry = false;
            toolbarMouseDown(pick);
            return;
        }
        switch (id) {
        case kQuitId:
            if (m_trashBg)
                return;
            showExitDialog(false);
            break;
        case 0:
            // The background: closes the trash dialog.
            if (!m_trashBg)
                return;
            closeTrash();
            break;
        case kGoId:
            if (m_trashBg || s_goCount++ != 0)
                return;
            if (m_selected != -1) {
                click("stable");
                tick();
                loadRecord(m_selected, &app().record());
                app().gotoNode("intromovie");
            } else if (m_entry) {
                const std::string playerName = m_field->text();
                if (playerName.empty()) {
                    m_selected = m_pending = -1;
                    return;
                }
                click("stable");
                tick();
                m_field->setLocked(true);
                m_selected = m_pending;
                saves::save(PlayerRecord::newPlayer(playerName), m_pending); // CreateNewSaveGame 0x409f20
                loadRecord(m_pending, &app().record());
                playVoice("db0468");
                s_goCount = 1; // "intromovie" when db0468 ends, or on the next Go
            }
            break;
        case kNewId:
            if (s_goCount != 0 || m_trashBg)
                return;
            click("newgame");
            m_selected = -1;
            m_pending = firstFreeSlot();
            m_blinkAnim = nullptr;
            if (m_pending == -1) {
                playVoice("db02044"); // every slot is taken
            } else {
                m_entry = true;
                showBigCard();
                m_field->setText("");
                m_field->setLocked(false);
                m_field->activate();
                playVoice("db03073");
                startGoBlink();
            }
            break;
        case kHelpId:
            if (s_goCount != 0)
                return;
            playVoice("db04015");
            playVoiceAfterPause("db03071");
            playVoiceAfterPause("db03072");
            break;
        case kDeleteId:
            if (s_goCount != 0 || m_trashBg || m_selected == -1)
                return;
            openTrash();
            break;
        case kTrashYesId:
            if (m_selected != -1) {
                m_cards[static_cast<std::size_t>(m_selected)]->hide();
                saves::remove(m_selected); // DeleteSavegame 0x429690
                m_pending = m_selected = -1;
                hideBigCard();
                m_field->setText("");
                click("trash");
            } else if (m_entry) {
                m_pending = m_selected = -1;
                hideBigCard();
                click("trash");
                m_entry = false;
            }
            m_blinkAnim = nullptr;
            if (!m_trashBg)
                return;
            closeTrash();
            break;
        case kTrashNoId:
            if (!m_trashBg)
                return;
            closeTrash();
            click("click");
            break;
        case kBigCardId:
            if (m_entry && m_field->text().empty())
                m_field->activate();
            break;
        default:
            break;
        }
        toolbarMouseDown(pick);
    }

    void onKeyDown(const engine::InputEvent& e) override {
        // The focused name field takes every key; otherwise KeyDown (0x429aa0).
        settleFocus();
        if (NameField::s_keyFocus) {
            NameField::s_keyFocus->key(e);
            return;
        }
        if (e.key == 0) {
            if (e.ch == '\r' && m_entry && m_field)
                m_field->activate();
        } else if (e.key == engine::key::kEscape) {
            closeTrash();
            showExitDialog(false);
        }
    }

    void onMouseDown(const engine::InputEvent& e) override {
        // The event translator (0x45ddc0) clears the key focus before every left
        // press; whoever had it loses it unless the press gave it back (-109).
        if (e.type != engine::InputEvent::LeftDown)
            return;
        settleFocus();
        m_focusBefore = NameField::s_keyFocus;
        NameField::s_keyFocus = nullptr;
        m_focusPending = true;
    }

    void listen(int message, const engine::MessageData& data) override {
        if (message == -102) {
            settleFocus(); // after the press went through the node
            return;
        }
        // ListenToMessage (0x4298b0): a press on the bare background ends the entry.
        if (message == kMousePress && m_entry && m_field) {
            const auto* pick = static_cast<const engine::Pick*>(data.ptr);
            if (pick && pick->result == engine::Pick::Background && pick->y < 430) {
                if (m_field->text().empty()) {
                    m_entry = false;
                    m_pending = m_selected = -1;
                    hideBigCard();
                    m_field->setText("");
                    m_blinkAnim = nullptr;
                } else {
                    m_field->setLocked(true);
                }
            }
        }
        ToolbarNode::listen(message, data);
    }

    void soundDone(const std::string& sound, const void*, bool) override {
        // 0x429cf0 (the toolbar's handler is not called).
        if (str::iequals(sound, "db0468") && s_goCount != 0)
            app().gotoNode("intromovie");
    }

    void tick() override {
        // SpendTime (0x429b40): keep the name centred on the big card.
        if (m_field && m_card) {
            const engine::Rect r = m_card->rect();
            m_field->setPosition(static_cast<float>(r.left) + static_cast<float>(r.width() / 2) -
                                     static_cast<float>(m_field->width() / 2),
                                 m_field->y(), m_field->z());
        }
        if (m_blinkAnim && engine().ticks() - m_blinkTick > kBlinkMs)
            setGoBlink(!m_blinkOn);
        if (s_goCount > 1) {
            s_goCount = 0;
            app().gotoNode("intromovie");
        }
        if (m_field)
            m_field->idle(); // the field's own idle task
        ToolbarNode::tick();
    }

private:
    // The constructor's buttons also get rectangle hits (anim+0x84 = 0); the
    // trash dialog's keep per-pixel hits.
    std::unique_ptr<RolloverButton> button(std::string_view anim, int clickId, bool rectangleHit = true) {
        auto b = std::make_unique<RolloverButton>(*this, false);
        b->init(anim);
        if (b->anim()) {
            b->anim()->setClickId(clickId);
            if (rectangleHit)
                b->anim()->setPerPixelHit(false);
        }
        return b;
    }

    void click(std::string_view sound) {
        engine().sounds().playSound(sound, -1, false, false, 1, false, 0x70, -1, 0, false);
    }

    // SlotExists (0x429440) and LoadGame (0x428080) also make the slot the
    // current one ([0x809ac0]).
    bool slotExists(int slot) {
        app().setSaveSlot(slot);
        return saves::exists(slot);
    }
    std::optional<PlayerRecord> loadRecord(int slot, PlayerRecord* into = nullptr) {
        app().setSaveSlot(slot);
        auto rec = saves::load(slot);
        if (rec && into)
            *into = *rec;
        else if (!rec)
            log::warn("SignIn: cannot load saved game {}", slot);
        return rec;
    }
    int firstFreeSlot() {
        for (int slot = 0; slot < kSlots; ++slot)
            if (!slotExists(slot))
                return slot;
        return -1;
    }

    void showBigCard() {
        if (m_card)
            m_card->setHideCount(0);
        m_field->setHideCount(0);
    }
    void hideBigCard() {
        if (m_card)
            m_card->hide(true);
        m_field->hide(true);
    }

    // 0x429c40 / 0x429c80: the Go button flashes (sequence 3) once a second,
    // seven times.
    void startGoBlink() {
        m_blinkAnim = m_go ? m_go->anim() : nullptr;
        if (m_blinkAnim)
            m_blinkAnim->setSequence(3, true);
        m_blinkCount = 0;
    }
    void setGoBlink(bool on) {
        m_blinkOn = on;
        if (on && m_blinkCount++ > 6) {
            m_blinkAnim = nullptr;
            return;
        }
        if (m_blinkAnim)
            m_blinkAnim->setSequence(on ? 3 : 0, true);
        m_blinkTick = engine().ticks();
    }

    void openTrash() {
        m_trashBg = createAnimAtViewport("trash_bg");
        m_trashYes = button("trash_yes", kTrashYesId, false);
        m_trashNo = button("trash_no", kTrashNoId, false);
    }
    void closeTrash() {
        if (!m_trashBg)
            return;
        engine().anims().remove(m_trashBg->name());
        m_trashYes.reset();
        m_trashNo.reset();
        m_trashBg = nullptr;
    }

    void settleFocus() {
        if (!m_focusPending)
            return;
        m_focusPending = false;
        if (NameField::s_keyFocus != m_focusBefore && m_focusBefore)
            m_focusBefore->lostFocus();
        m_focusBefore = nullptr;
    }

    bool m_entry = false;                          // +0x1C0: typing a new name
    engine::Anim* m_card = nullptr;                // +0x1C4 "signincard"
    std::unique_ptr<NameField> m_field;            // +0x1C8
    std::shared_ptr<engine::Font> m_nameFont;      // +0x1CC
    std::shared_ptr<engine::Font> m_cardFont;      // +0x1D0
    NameField::Colors m_colors{};
    int m_pending = -1;                            // +0x1D4: slot for the new player
    int m_selected = -1;                           // +0x1D8
    std::array<std::unique_ptr<SignInCard>, kSlots> m_cards;       // +0x1DC
    std::array<std::unique_ptr<ACRollover>, kSlots> m_cardRollovers; // +0x204
    std::unique_ptr<RolloverButton> m_quit, m_help, m_delete, m_new, m_go; // +0x22C..+0x23C
    engine::Anim* m_blinkAnim = nullptr;           // +0x240
    int m_blinkCount = 0;                          // +0x244
    bool m_blinkOn = false;                        // +0x246
    std::uint32_t m_blinkTick = 0;                 // +0x248
    bool m_noSaves = true;                         // +0x24C
    engine::Anim* m_trashBg = nullptr;             // +0x250
    std::unique_ptr<RolloverButton> m_trashYes, m_trashNo; // +0x254 / +0x258

    NameField* m_focusBefore = nullptr;
    bool m_focusPending = false;
};

BRC_REGISTER_NODE("signin", [](Godiva& app, const std::string& name) { return std::make_unique<SignIn>(app, name); });

} // namespace
} // namespace brc::game
