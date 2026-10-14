// Grooming (grooming.cpp, class Grooming, ctor 0x413940): the horse in close-up
// with the comb and the wet brush on the wall. The comb is pulled through the
// mane and tail; the wet brush darkens the coat where it passes (it paints
// holes into the light coat layer over the dark one) and the shine slowly
// comes back. The edges lead back to the tack room. See docs/engine/nodes.md
// "Grooming".
#include "core/Log.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/SoundManager.h"
#include "game/ToolbarNode.h"
#include "game/Widgets.h"

#include <array>
#include <memory>
#include <string>

namespace brc::game {
namespace {

// Click ids (anim +0x11c).
constexpr int kCombClick = 0x5B7E, kBrushClick = 0x5B7F, kHeadClick = 0x5B81, kBeamClick = 0x5B82;

// Tool states (+0x1C4) and sub-states ([0x809c24]).
constexpr int kNoTool = 0, kComb = 2, kCombOnHorse = 3, kBrush = 4, kBrushOnBody = 5;

// Per-coat assets by the player's horse (0 palomino, 1 white, 2 paint, 3 dapple).
struct Coat {
    std::string_view cache, comb, tailComb, head, tail, bodyDark, bodyLight, bodyLight2;
};
constexpr std::array<Coat, 5> kCoats = {{
    {"groomingpal.omt.cache", "TackGroomCombPal", "TackGroomTailCombPal", "GroomHeadPalAmb",
     "GroomTailPalAmb", "TackGroomBodyDarkPal", "TackGroomBodyLightPal", "TackGroomBodyLightPal2"},
    {"groomingwht.omt.cache", "TackGroomCombWhite", "TackGroomTailCombWhite", "GroomHeadWhiteAmb",
     "GroomTailWhiteAmb", "TackGroomBodyDarkWhite", "TackGroomBodyLightWhite", "TackGroomBodyLightWhite2"},
    {"groomingpnt.omt.cache", "TackGroomCombPaint", "TackGroomTailCombPaint", "GroomHeadPaintAmb",
     "GroomTailPaintAmb", "TackGroomBodyDarkPaint", "TackGroomBodyLightPaint", "TackGroomBodyLightPaint2"},
    {"groomingdap.omt.cache", "TackGroomCombDap", "TackGroomTailCombDap", "GroomHeadDapAmb",
     "GroomTailDapAmb", "TackGroomBodyDarkDap", "TackGroomBodyLightDap", "TackGroomBodyLightDap2"},
    // No horse: the paint, but the copy of its light coat is named after the dapple's (sic).
    {"groomingpnt.omt.cache", "TackGroomCombPaint", "TackGroomTailCombPaint", "GroomHeadPaintAmb",
     "GroomTailPaintAmb", "TackGroomBodyDarkPaint", "TackGroomBodyLightPaint", "TackGroomBodyLightDap2"},
}};

// Session globals of the original.
int s_subState = kNoTool;                 // [0x809c24]
std::uint32_t s_hintTick = 0;             // [0x809c20]: last hint voice (30 s nag)
bool s_firstCombVoice = true;             // [0x7ffff0]
bool s_firstBrushVoice = true;            // [0x7ffff4]
float s_lastMouseX = 0, s_lastMouseY = 0; // [0x809c18] / [0x809c1c]
bool s_shineStarted = false;              // [0x809c10] bit 0
std::uint32_t s_lastShineTick = 0;        // [0x809c0c]

// The inline "show" of the original: a hidden anim gets its hide counter set
// to 1, then Show.
void show(engine::Anim* a) {
    if (a && a->hideCount() != 0) {
        a->setHideCount(1);
        a->hide(false);
    }
}

class Grooming final : public ToolbarNode {
public:
    Grooming(Godiva& app, const std::string& name) : ToolbarNode(app, name) {
        // 0x413940.
        engine().anims().preloadCache("grooming.omt.cache");
        setSkin(kIn);
        m_buttonHeld = false;
        addToolbarButton("tbQuitIn", kQuit);
        addToolbarButton("tbPagerIn", kPager);
        addToolbarButton("tbCameraIn", kCamera);
        addToolbarButton("tbMapIn", kMap);
        addToolbarButton("tbBagIn", kBag);
        addToolbarButton("tbHelpIn", kHelp);
        addToolbarButton("tbHomeIn", kHome);
        moveRollovers(cameraX(), cameraY());
        setCursor("cursorIn");
        edgeScroll = true;
        if (!ambientLoop().empty())
            releaseAmbient();
        playAmbientLoop("stableamb2", 0x28);
        createCoatAnims();
        playVoiceNoFlaps("Pause");
        playVoiceNoFlaps("Pause");
        playVoiceNoFlaps("DB03058");
        playVoiceNoFlaps("sPause");
        listenToMouseButtons(); // [0x80a5a0]+0xC
    }

    ~Grooming() override {
        // 0x413b20.
        m_brushRollover.reset();
        m_combRollover.reset();
        releaseAmbient();
        auto& anims = engine().anims();
        for (auto n : {"Beam", "TackGroomBrush", "TackGroomBrushCursor", "TackGroomBrushMask",
                       "TackGroomBrushWet", "TackGroomBrushWetUp", "TackGroomBucket", "TackGroomComb",
                       "TackGroomCombCursor", "TackGroomCombWall", "posG"})
            anims.remove(n);
    }

    // MouseDown (0x413c40).
    void onPress(const engine::Pick& pick) override {
        switch (pick.element ? pick.element->clickId() : 0) {
        case kCombClick:
            // The comb comes off the wall (the brush goes back).
            m_tool = kComb;
            takeTool("TackGroomCombCursor");
            if (m_comb)
                m_comb->hide(true);
            show(m_brush);
            playVoice("DB0540");
            break;
        case kBrushClick:
            if (m_brush)
                m_brush->hide(true);
            m_tool = kBrush;
            takeTool("TackGroomBrushWetUp");
            s_hintTick = engine().ticks();
            show(m_comb);
            playVoice("DB02053");
            break;
        case kHeadClick: {
            // A pat on the nose.
            const auto [x, y] = mouseWorldPos();
            if (x > 470.0f) {
                if (m_head)
                    m_head->setSequence(1, true);
                static constexpr std::array<std::string_view, 6> kWhinnies = {
                    "Whinney1", "Whinney2", "Whinney3", "Whinney4", "Whinney5", "Snort"};
                engine().sounds().playSound(kWhinnies[static_cast<std::size_t>(engine().rand() % 6)], -1,
                                            false, false, 1, false, 0x70, -1, 0, false);
            }
            break;
        }
        case kBeamClick:
            // The tool goes back on the wall.
            setCursor("cursorIn");
            edgeScroll = true;
            show(m_brush);
            show(m_comb);
            m_tool = kNoTool;
            break;
        case kHelpClickId:
            playVoice("Db04110");
            break;
        default:
            break;
        }
        toolbarMouseDown(pick);
    }

    // ListenToMessage (0x413fc0): the button state, and the edges.
    void listen(int message, const engine::MessageData& data) override {
        if (message == -103) {
            m_buttonHeld = false;
            m_lastPositions.fill(-1.0f);
            engine().sounds().stopSound("brushing");
        } else if (message == -102) {
            m_buttonHeld = true;
        } else if (message == kMsgEdgeScroll) {
            gotoNode("tackroom");
        }
        ToolbarNode::listen(message, data);
    }

    // AnimEnded (0x414070): the head and tail go back to their idle sequence.
    void sequenceEnded(engine::Anim& anim, int seq) override {
        if (&anim == m_head)
            m_head->setSequence(0, true);
        else if (&anim == m_tail)
            m_tail->setSequence(0, true);
        ToolbarNode::sequenceEnded(anim, seq);
    }

    // SpendTime (0x4140f0).
    void tick() override {
        const auto [x, y] = mouseWorldPos();
        const std::uint32_t now = engine().ticks();
        // Moving to the right edge without the button down puts the tool away.
        if (!m_buttonHeld && x > 620.0f && m_tool != kNoTool) {
            setCursor("cursorIn");
            edgeScroll = true;
            show(m_brush);
            show(m_comb);
            m_tool = kNoTool;
        }
        int sub = s_subState;
        switch (m_tool) {
        case kComb:
        case kCombOnHorse:
            sub = combTick(x, y, now);
            break;
        case kBrush:
        case kBrushOnBody:
            if (!m_buttonHeld || !getCollision(m_bodyLight, cursor())) {
                sub = kBrush;
                if (s_subState != kBrush)
                    takeTool("TackGroomBrushWetUp", false);
            } else {
                if (s_subState != kBrushOnBody)
                    takeTool("TackGroomBrushWet", false);
                s_subState = kBrushOnBody;
                paintWetBrush();
                if (!s_firstBrushVoice) {
                    if (now - s_hintTick > 30000) {
                        s_hintTick = engine().ticks();
                        if (engine().rand() % 2 == 0)
                            playVoice("DB0542");
                        else
                            sayHorseNameThen("DB0545");
                    }
                } else {
                    s_firstBrushVoice = false;
                    sayHorseNameThen("DB0544");
                    s_hintTick = engine().ticks();
                }
                sub = s_subState;
            }
            break;
        default:
            break;
        }
        s_subState = sub;
        updateShine();
        ToolbarNode::tick();
    }

private:
    // The cursor becomes a tool (always drawn on top: keep-depth flag).
    void takeTool(std::string_view cursorName, bool front = true) {
        edgeScroll = false;
        setCursor(cursorName);
        if (engine::Anim* c = cursor()) {
            c->addHeaderFlags(formats::anim_flags::kKeepDepth);
            c->recenter();
            if (front)
                c->setPosition(c->x(), c->y(), 640000.0f);
        }
    }

    bool mouseMoved(float x, float y) {
        if (x == s_lastMouseX && y == s_lastMouseY)
            return false;
        s_lastMouseY = y;
        s_lastMouseX = x;
        return true;
    }

    void brushingSound(int loops) {
        auto& sounds = engine().sounds();
        if (!sounds.isPlaying("brushing"))
            sounds.playSound("brushing", -1, false, false, loops, false, 0x70, -1, 0, false);
    }

    // The comb part of SpendTime: on the mane (head), the body, the tail, or in the air.
    int combTick(float x, float y, std::uint32_t now) {
        if (m_buttonHeld && getCollisionContains(m_head, cursor())) {
            if (s_subState != kCombOnHorse) {
                if (x <= 490.0f || (m_head && m_head->sequence() == 1)) {
                    // Combing the mane.
                    edgeScroll = false;
                    setCursor(m_combName);
                    if (engine::Anim* c = cursor()) {
                        c->addHeaderFlags(formats::anim_flags::kKeepDepth);
                        c->recenter();
                        c->setSequence(0, true);
                        c->setPosition(c->x(), c->y(), 50000.0f);
                    }
                    if (mouseMoved(x, y))
                        brushingSound(1);
                } else {
                    // Too close to the face: the horse shies.
                    if (m_head)
                        m_head->setSequence(1, true);
                    takeTool("TackGroomCombCursor", false);
                }
            }
            return combedHorse(now);
        }
        if (m_buttonHeld && getCollisionContains(m_bodyLight, cursor())) {
            if (mouseMoved(x, y))
                brushingSound(1);
            return s_subState;
        }
        if (m_buttonHeld && x > 69.0f && x < 90.0f && y > 235.0f && y < 400.0f) {
            if (s_subState != kCombOnHorse) {
                // Combing the tail.
                edgeScroll = false;
                setCursor(m_tailCombName);
                if (engine::Anim* c = cursor()) {
                    c->addHeaderFlags(formats::anim_flags::kKeepDepth);
                    c->setSequence(1, true);
                    c->recenter();
                }
                brushingSound(1);
            }
            return combedHorse(now);
        }
        takeTool("TackGroomCombCursor", false);
        s_subState = kComb;
        if (m_buttonHeld && getCollisionContains(m_bodyLight, cursor()))
            brushingSound(0);
        if (now - s_hintTick > 30000) {
            s_hintTick = engine().ticks();
            if (engine().rand() % 2 == 0)
                sayHorseNameThen("DB00546");
            else
                playVoiceNoFlaps("DB0542");
        }
        return kComb;
    }

    // The first time the comb touches the mane or the tail.
    int combedHorse(std::uint32_t) {
        if (s_firstCombVoice) {
            s_firstCombVoice = false;
            sayHorseNameThen("DB0543");
            s_hintTick = engine().ticks();
        }
        return kCombOnHorse;
    }

    // CreateCoatAnims (0x4147d0).
    void createCoatAnims() {
        m_beam = createAnimAtViewport("Beam");
        const std::uint32_t horse = app().record().horse();
        const Coat& coat = kCoats[horse <= 3 ? horse : 4];
        engine().anims().preloadCache(coat.cache);
        m_combName = coat.comb;
        m_tailCombName = coat.tailComb;
        m_head = createAnimAtViewport(coat.head);
        m_tail = createAnimAtViewport(coat.tail);
        m_posG = createAnimAtViewport("posG");
        m_bodyDark = createAnimAtViewport(coat.bodyDark);
        m_bodyLight = createAnimAtViewport(coat.bodyLight);
        // A second instance keeps the untouched coat for the shine to come back from.
        m_bodyLight2 = createAnimAtViewport(coat.bodyLight, coat.bodyLight2);
        m_brush = createAnimAtViewport("TackGroomBrush");
        m_comb = createAnimAtViewport("TackGroomComb");
        if (m_bodyLight2)
            m_bodyLight2->hide(true);
        for (auto* a : {m_head, m_bodyLight, m_bodyLight2, m_bodyDark, m_tail})
            if (a)
                a->setPerPixelHit(true);
        if (m_bodyLight)
            m_bodyLight->setPosition(m_bodyLight->x(), m_bodyLight->y(), 500.0f);
        if (m_bodyDark)
            m_bodyDark->setPosition(m_bodyDark->x(), m_bodyDark->y(), 499.0f);
        if (m_tail)
            m_tail->setPosition(m_tail->x(), m_tail->y(), 498.0f);
        if (m_head)
            m_head->setPosition(m_head->x(), m_head->y(), 498.0f);
        m_combCursor = createAnimAtViewport(m_combName);
        if (m_combCursor) {
            m_combCursor->recenter();
            m_combCursor->hide(true);
        }
        if (m_posG)
            m_posG->setPosition(-274.0f, -64.0f, m_posG->z());
        if (m_comb)
            m_comb->setClickId(kCombClick);
        m_combRollover = std::make_unique<ACRollover>(*this, m_comb, 7);
        if (m_brush)
            m_brush->setClickId(kBrushClick);
        m_brushRollover = std::make_unique<ACRollover>(*this, m_brush, 7);
        if (m_head)
            m_head->setClickId(kHeadClick);
        if (m_beam)
            m_beam->setClickId(kBeamClick);
    }

    // PaintWetBrush (0x414e30): a slanted 40x18 stroke around the pointer
    // becomes transparent in the light coat (the colour of its first pixel),
    // so the dark, wet coat shows through.
    void paintWetBrush() {
        auto& sounds = engine().sounds();
        if (!sounds.isPlaying("bubbles2"))
            sounds.playSound("bubbles2", -1, false, false, 1, false, 50, -1, 0, false); // volume [0x7f1ac0]
        engine::Image8* img = m_bodyLight ? m_bodyLight->smackerCanvas() : nullptr;
        if (!img || img->pixels.empty())
            return;
        const auto [mx, my] = mouseWorldPos();
        const engine::Rect r = m_bodyLight->screenRect(cameraX(), cameraY());
        const float lx = mx - static_cast<float>(r.left);
        const float ly = my - static_cast<float>(r.top);
        const int pitch = img->width;
        const int cx = static_cast<int>(lx), cy = static_cast<int>(ly);
        for (int x = cx - 20; x < cx + 20; ++x) {
            for (int y = cy - 9; y < cy + 9; ++y) {
                const int index = (pitch + 1) * y + x - cy; // row y, column x + (y - cy)
                const int column = static_cast<int>(static_cast<float>(x + y) - ly);
                if (index > 0 && index < img->height * pitch && column < img->width && column >= 0)
                    img->pixels[static_cast<std::size_t>(index)] = img->pixels[0];
            }
        }
        m_bodyLight->move(1.0f, 0, 0); // marks it for redrawing
        m_bodyLight->move(-1.0f, 0, 0);
    }

    // UpdateShine (0x415020): every 60 ms, 150 random 4-pixel runs of the
    // untouched coat are copied back into the light coat.
    void updateShine() {
        const std::uint32_t now = engine().ticks();
        if (!s_shineStarted) {
            s_shineStarted = true;
            s_lastShineTick = now;
        }
        if (now - s_lastShineTick < 60)
            return;
        s_lastShineTick = now;
        engine::Image8* dst = m_bodyLight ? m_bodyLight->smackerCanvas() : nullptr;
        engine::Image8* src = m_bodyLight2 ? m_bodyLight2->smackerCanvas() : nullptr;
        if (!dst || !src || dst->pixels.empty())
            return;
        const int size = static_cast<int>(static_cast<double>(src->height * src->width) - 3.0);
        if (size <= 0 || dst->pixels.size() < src->pixels.size())
            return;
        for (int i = 0; i < 150; ++i) {
            const int hi = engine().rand();
            const int lo = engine().rand();
            const int at = (lo + hi * 0x7FFF) % size;
            for (int k = 0; k < 4; ++k)
                dst->pixels[static_cast<std::size_t>(at + k)] = src->pixels[static_cast<std::size_t>(at + k)];
        }
        m_bodyLight->move(1.0f, 0, 0);
        m_bodyLight->move(-1.0f, 0, 0);
    }

    int m_tool = kNoTool;                                             // +0x1C4
    std::string m_combName;                                           // +0x1C8: the comb-in-the-mane cursor
    std::string m_tailCombName;                                       // +0x1E8: the comb-in-the-tail cursor
    engine::Anim* m_bodyLight = nullptr;                              // +0x208
    engine::Anim* m_bodyLight2 = nullptr;                             // +0x20C (hidden)
    engine::Anim* m_bodyDark = nullptr;                               // +0x210
    engine::Anim* m_head = nullptr;                                   // +0x214 GroomHead<Coat>Amb
    engine::Anim* m_tail = nullptr;                                   // +0x218 GroomTail<Coat>Amb
    engine::Anim* m_combCursor = nullptr;                             // +0x21C
    engine::Anim* m_comb = nullptr;                                   // +0x220 on the wall
    engine::Anim* m_brush = nullptr;                                  // +0x228 on the wall
    engine::Anim* m_beam = nullptr;                                   // +0x230: the wall (puts the tool back)
    engine::Anim* m_posG = nullptr;                                   // +0x234
    std::unique_ptr<ACRollover> m_combRollover;                       // +0x238
    std::unique_ptr<ACRollover> m_brushRollover;                      // +0x23C
    bool m_buttonHeld = false;                                        // +0x244
    std::array<float, 4> m_lastPositions{-1.0f, -1.0f, -1.0f, -1.0f}; // +0x248..+0x254 (never read)
};

} // namespace

BRC_REGISTER_NODE("grooming",
                  [](Godiva& app, const std::string& name) { return std::make_unique<Grooming>(app, name); });

} // namespace brc::game
