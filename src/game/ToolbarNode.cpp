#include "game/ToolbarNode.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/Scripts.h"
#include "game/Popups.h"
#include "game/Widgets.h"

#include <array>
#include <cstring>
#include <format>

namespace brc::game {
namespace {

// The 34 shared HUD anims of commonin/commonout.omt.cache, in the order of the
// globals 0x809fa8..0x80a02c (HCToolbarNode ctor 0x41b690).
constexpr std::array<std::string_view, 34> kHudAnims = {
    "christieLCDIn", "pagerIn",   "pagerNoIn",     "pagerRaceIn", "pagerReplayIn", "pagerYesIn",   "tbQuitIn",
    "teresaLCDIn",   "cursorIn",  "saddlebagIn",   "sbFlapIn",    "tbBagIn",       "tbCameraIn",   "tbHelpIn",
    "tbHomeIn",      "tbMapIn",   "tbPagerIn",     "christieLCDOut", "pagerNoOut", "pagerOut",     "pagerRaceOut",
    "pagerReplayOut", "pagerYesOut", "tbQuitOut",  "teresaLCDOut", "cursorOut",    "saddlebagOut", "sbFlapOut",
    "tbBagOut",      "tbCameraOut", "tbHelpOut",   "tbHomeOut",   "tbMapOut",      "tbPagerOut"};

void unlinkHudAnims(engine::AnimRegistry& anims) {
    for (auto name : kHudAnims)
        if (engine::Anim* a = anims.find(name))
            a->unlink();
}

} // namespace

ToolbarNode::ToolbarNode(Godiva& app, const std::string& name) : engine::Node(app, name), m_app(app) {
    // 0x41b690. The shared HUD anims are created once per session (global
    // 0x809fa8 == 0) from the two cache files, then unlinked so they survive
    // node changes and join whichever node asks for them next.
    auto& anims = engine().anims();
    if (!anims.find(kHudAnims[0])) {
        anims.preloadCache("commonin.omt.cache");
        anims.preloadCache("commonout.omt.cache");
        for (auto n : kHudAnims)
            getAnim(n);
        unlinkHudAnims(anims);
    }
    m_app.idleTimer().resetInterval(); // app+0x19C vt+0x18
}

ToolbarNode::~ToolbarNode() {
    // 0x41bc30.
    engine().sounds().stopChannel(14);
    releaseAmbient();
    // The toolbar buttons are the shared HUD anims: delete the buttons, keep the anims.
    for (auto& b : m_buttons)
        b->releaseAnim();
    m_buttons.clear();
    unlinkHudAnims(engine().anims());
    closePopup();
    m_pager.reset();
    m_saddlebag.reset();
    if (m_exitDialog)
        closeExitDialog();
    if (Rollover::liveCount() > 0)
        log::debug("HCToolbarNode::~HCToolbarNode() - you're not deleting all your Rollovers!");
    if (ACRollover::liveCount() > 0)
        log::debug("HCToolbarNode::~HCToolbarNode() - you're not deleting all your ACRollovers!");
}

// --- skin, cursor, toolbar ------------------------------------------------------------------

std::string_view ToolbarNode::skinSuffix() const {
    switch (m_skin) {
    case kOut: return "Out";
    case kFrog: return "Frog";
    case kSign: return "Sign";
    case kVine: return "Vine";
    default: return "In";
    }
}

void ToolbarNode::setCursor(std::string_view name) {
    // 0x41beb0.
    if (m_cursor)
        m_cursor->hide(true);
    m_cursor = getAnim(name);
    if (m_cursor) {
        while (m_cursor->hideCount() != 0)
            m_cursor->hide(false);
        m_cursor->setPickable(false); // +0x62 = 1: clicks go to what is under it
    }
    edgeLeft = edgeRight = edgeBoth = edgeScroll = false;
}

RolloverButton* ToolbarNode::addToolbarButton(std::string_view anim, int id) {
    // 0x41bf40.
    auto button = std::make_unique<RolloverButton>(*this, false);
    button->init(anim);
    engine::Anim* a = button->anim();
    if (a) {
        a->setElementId(id);
        a->setPerPixelHit(false);
        a->setPosition(cameraX(), cameraY(), 3000.0f);
    }
    RolloverButton* raw = button.get();
    m_buttons.push_back(std::move(button));
    if (id == kPager) {
        if (!m_pager)
            m_pager = std::make_unique<BarbiePager>(*this);
        m_pager->setButtonAnim(a);
    } else if (id == kBag) {
        if (!m_saddlebag) {
            m_saddlebag = std::make_unique<BarbieSaddlebag>(*this);
            m_saddlebag->restoreFromRecord();
            m_saddlebag->build();
            m_saddlebag->close();
        }
    } else if (id == kHelp && a) {
        a->setClickId(kHelpClickId);
    }
    return raw;
}

void ToolbarNode::showToolbar(bool show, bool redraw) {
    // 0x41c190.
    for (auto& b : m_buttons)
        b->setEnabled(show);
    if (m_cursor)
        m_cursor->hide(!show);
    if (redraw)
        updateFrame(true);
}

void ToolbarNode::moveRollovers(float x, float y) {
    // 0x41c1f0.
    for (auto& b : m_buttons)
        if (b->anim() && b->anim()->visible())
            b->anim()->setPosition(x, y, 3000.0f);
}

RolloverButton* ToolbarNode::findButtonById(int id) {
    for (auto& b : m_buttons)
        if (b->anim() && b->anim()->elementId() == id)
            return b.get();
    return nullptr;
}

void ToolbarNode::onToolbarButton(const engine::Pick& pick) {
    // 0x41c530 (runs for presses and releases).
    if (!pick.element)
        return;
    int id = -1;
    for (auto& b : m_buttons) {
        if (b->anim() == pick.element) {
            id = b->anim()->elementId();
            break;
        }
    }
    if (id == -1)
        return;
    m_app.popupRequest = 0;
    auto leaveMiniGame = [&](std::uint32_t request) {
        m_app.popupRequest = request;
        m_app.session().arriveFromMiniGame = 1;
        m_app.gotoNode(nodeName(miniGameReturn));
    };
    switch (id) {
    case kQuit:
        showExitDialog(true);
        break;
    case kPager:
        if (miniGame) {
            leaveMiniGame(1);
        } else if (!pagerClickEnabled) {
            pagerClickEnabled = true;
        } else {
            openPopup(popup::kPager);
        }
        break;
    case kCamera:
        takePhoto(pick);
        break;
    case kMap:
        if (miniGame)
            leaveMiniGame(3);
        else if (m_app.record().hasHorse())
            openPopup(popup::kMap);
        else
            m_app.gotoNode("stable");
        break;
    case kBag:
        if (miniGame)
            leaveMiniGame(4);
        else
            openPopup(popup::kSaddlebag);
        break;
    case kHome:
        if (m_app.record().hasHorse())
            m_app.gotoNode("tackroom");
        else if (name() != "stable")
            m_app.gotoNode("stable");
        break;
    default:
        break; // the Help button (5) is handled by each scene's own onPress
    }
}

void ToolbarNode::takePhoto(const engine::Pick& pick) {
    // 0x41c840: on left-button release only.
    if (pick.eventType != engine::InputEvent::LeftUp)
        return;
    listen(kMsgCamera, {}); // the trail missions C1-C3 react to it
    auto shutter = [&] { engine().sounds().playSound("camera", -1, false, false, 1, false, 0x70, -1, 0, false); };
    if (skipNextPhoto) {
        shutter();
        skipNextPhoto = false;
        return;
    }
    showToolbar(false, true);
    if (camera::takePhoto(m_app))
        shutter();
    showToolbar(true, true);
}

// --- popups ------------------------------------------------------------------------------------

void ToolbarNode::openPopup(int kind) {
    // 0x41c760.
    Popup* p = nullptr;
    if (kind == popup::kPager)
        p = m_pager.get();
    else if (kind == popup::kMap)
        p = &m_app.map();
    else if (kind == popup::kSaddlebag)
        p = m_saddlebag.get();
    if (!p) {
        log::warn("{}: popup {} requested without its toolbar button", name(), kind);
        return;
    }
    m_popupKind = kind;
    m_popup = p;
    p->open(*this);
}

void ToolbarNode::closePopup() {
    // 0x41c7e0.
    if (m_popupKind != 0) {
        m_popupKind = 0;
        Popup* p = m_popup;
        m_popup = nullptr;
        if (p)
            p->close();
    }
}

void ToolbarNode::popupMouseDown(const engine::Pick& pick) {
    // 0x41c810.
    if (m_popup && m_popup->mouseDown(pick))
        closePopup();
}

// --- exit dialog ----------------------------------------------------------------------------------

void ToolbarNode::showExitDialog(bool save) {
    // 0x41e150.
    if (m_exitDialog)
        return;
    const std::string sfx(skinSuffix());
    m_app.session().saveOnQuit = save ? 1 : 0;
    m_exitDialog = true;
    m_exitBg = getAnim("exit_bg" + sfx);
    auto button = [&](std::string_view base, int clickId) {
        auto b = std::make_unique<RolloverButton>(*this, false);
        b->init(std::string(base) + sfx);
        if (b->anim())
            b->anim()->setClickId(clickId);
        return b;
    };
    m_exitYes = button("exit_yes", kExitYes);
    m_exitNo = button("exit_no", kExitNo);
    m_exitCredits = button("exit_credits", kExitCredits);
    m_exitBorder = getAnim("exitborder" + sfx);
    if (m_exitBorder)
        m_exitBorder->setPerPixelHit(false);
    playVoice("db0577");
}

void ToolbarNode::closeExitDialog() {
    // The "No" branch of 0x41c340 (and the destructor).
    m_exitYes.reset();
    m_exitNo.reset();
    m_exitCredits.reset();
    auto& anims = engine().anims();
    if (m_exitBg)
        anims.remove(m_exitBg->name());
    if (m_exitBorder)
        anims.remove(m_exitBorder->name());
    m_exitBg = m_exitBorder = nullptr;
    m_exitDialog = false;
}

void ToolbarNode::quitGame() {
    // 0x41e630. The Desktop Horse (copying the horse bitmaps and the shortcut
    // to Horse.exe, a separate Windows program) is not part of OpenBRC.
    log::info("quit: Desktop Horse {} (not supported)", m_app.session().desktopHorse ? "requested" : "off");
    m_app.quitGame(m_app.session().saveOnQuit != 0);
}

void ToolbarNode::toolbarMouseDown(const engine::Pick& pick) {
    // 0x41c340.
    if (!m_exitDialog) {
        if (m_popupKind != 0)
            popupMouseDown(pick);
        else
            onToolbarButton(pick);
        return;
    }
    if (!pick.element)
        return;
    switch (pick.element->clickId()) {
    case kExitYes:
        closePopup();
        quitGame();
        break;
    case kExitNo:
        closeExitDialog();
        break;
    case kExitCredits:
        m_app.gotoNode("credits");
        break;
    default:
        break;
    }
}

void ToolbarNode::onPress(const engine::Pick& pick) { toolbarMouseDown(pick); }
void ToolbarNode::onRelease(const engine::Pick& pick) { toolbarMouseDown(pick); }

void ToolbarNode::onKeyDown(const engine::InputEvent& e) {
    // 0x41c310: Escape opens the exit dialog (with save).
    if (e.key == engine::key::kEscape)
        showExitDialog(true);
}

// --- pointer, scrolling ----------------------------------------------------------------------------

std::pair<float, float> ToolbarNode::mouseWorldPos() const {
    // 0x41dda0.
    const auto [x, y] = const_cast<ToolbarNode*>(this)->engine().mousePosition();
    return {static_cast<float>(x - viewRect().left), static_cast<float>(y - viewRect().top)};
}

void ToolbarNode::setScrollVelocity(float vx, float vy) {
    // 0x41cb40.
    if (vx != m_scrollVx || vy != m_scrollVy)
        m_scrollTick = engine().ticks();
    m_scrollVx = vx;
    m_scrollVy = vy;
}

void ToolbarNode::listenToMouseButtons() { engine().mouseListeners().addListener(this); }

void ToolbarNode::listen(int message, const engine::MessageData& data) {
    // 0x41c910.
    if (message == kMousePress) {
        if (m_popupKind != 0) {
            const auto* pick = static_cast<const engine::Pick*>(data.ptr);
            int id = -1;
            for (auto& b : m_buttons) {
                if (pick && b->anim() == pick->element) {
                    id = b->anim()->elementId();
                    break;
                }
            }
            if (id == kPager && m_pager && m_popup == m_pager.get()) {
                pagerClickEnabled = false; // the next pager-button click only re-arms
            } else if (m_popup && pick && !m_popup->hitTest(pick->x, pick->y)) {
                closePopup();
            }
        }
    } else if (message == -103) { // raw mouse button up (event manager broadcaster)
        const auto* e = static_cast<const engine::InputEvent*>(data.ptr);
        if (e && e->type == engine::InputEvent::LeftUp) {
            const int x = engine().mousePosition().first - viewRect().left;
            if (!edgeScroll || m_exitDialog) {
                if (edgeLeft) {
                    if (x < 15)
                        listen(kMsgEdgeLeft, {});
                } else if (edgeRight) {
                    if (x > 604)
                        listen(kMsgEdgeRight, {});
                } else if (edgeBoth) {
                    if (x < 15)
                        listen(kMsgEdgeLeft, {});
                    else if (x > 604)
                        listen(kMsgEdgeRight, {});
                } else if (edgeRightAlt && x > 604) {
                    listen(kMsgEdgeRightAlt, {});
                }
            } else if (x < 15 || x > 604) {
                listen(kMsgEdgeScroll, {});
            }
        }
    }
    engine::Node::listen(message, data);
}

void ToolbarNode::tick() {
    // SpendTime (0x41cba0).
    const auto [mx, my] = engine().mousePosition();
    m_mouseX = static_cast<float>(mx - viewRect().left);
    m_mouseY = static_cast<float>(my - viewRect().top);

    const bool busy = (m_saddlebag && m_saddlebag->isShowing()) || (m_pager && m_pager->isShowing()) ||
                      m_app.map().isShowing();
    if ((m_scrollVx != 0.0f || m_scrollVy != 0.0f) && !busy) {
        // Panorama scrolling: x += (now - last) * 0.01 * speed, within the limits.
        const std::uint32_t now = engine().ticks();
        const int viewW = viewRect().right - viewRect().left;
        float x = static_cast<float>(now - m_scrollTick) * 0.01f * m_scrollVx + cameraX();
        if (static_cast<float>(m_panRight) <= static_cast<float>(viewW) + x)
            x = static_cast<float>(viewRect().left - viewRect().right + m_panRight);
        if (x <= static_cast<float>(m_panLeft))
            x = static_cast<float>(m_panLeft);
        setCamera(x, cameraY());
        moveRollovers(cameraX(), cameraY());
        m_scrollTick = now;
    }

    if (engine::Anim* c = m_cursor) {
        // Edge arrows (input.md §8.2).
        const int seq = c->sequence();
        const float x = m_mouseX;
        if (c->visible() && seq != 7 && seq != 8) {
            if (x < 15.0f || x > 604.0f || seq == 0) {
                if (edgeScroll) {
                    if (x < 15.0f)
                        c->setSequence(1, true);
                    else if (x > 604.0f)
                        c->setSequence(2, true);
                } else if (edgeLeft) {
                    if (x < 15.0f)
                        c->setSequence(3, true);
                } else if (edgeRight) {
                    if (x > 604.0f)
                        c->setSequence(4, true);
                } else if (edgeBoth) {
                    if (x < 15.0f)
                        c->setSequence(3, true);
                    else if (x > 604.0f)
                        c->setSequence(4, true);
                } else if (edgeRightAlt && x > 604.0f && seq != 6) {
                    c->setPlaybackPauseCount(0);
                    c->setMotionPauseCount(0);
                    c->setSequence(6, true);
                }
            } else {
                c->setSequence(0, true);
            }
        }
        // Keep the cursor image on screen, then follow the pointer.
        const engine::Image8* img = c->image();
        const int w = img ? img->width : 0, h = img ? img->height : 0;
        if (!edgeLeft && !edgeScroll && !edgeRight && !edgeRightAlt && !edgeBoth) {
            if (m_mouseX > 639.0f)
                m_mouseX = 639.0f;
        } else if (static_cast<float>(w) + m_mouseX > 639.0f) {
            m_mouseX = static_cast<float>(639 - w);
        }
        if (static_cast<float>(h) + m_mouseY > 479.0f)
            m_mouseY = static_cast<float>(479 - h / 2);
        m_mouseX += cameraX();
        m_mouseY += cameraY();
        c->setPosition(m_mouseX, m_mouseY, c->z());
    }

    if (!m_voicePlaying) {
        if (closePagerRequested && m_popupKind != 0 && m_pager && m_popup == m_pager.get()) {
            closePagerRequested = false;
            closePopup();
        }
    } else {
        pollVoiceDone();
    }
    engine::Node::tick();
}

// --- navigation --------------------------------------------------------------------------------

void ToolbarNode::gotoNodeById(int id) {
    // 0x41de40.
    switch (id) {
    case 7: m_app.gotoNode("tackroom"); break;
    case 0x11: m_app.gotoNode("appleW"); break;
    case 0x13: m_app.gotoNode("bridgeS"); break;
    case 0x14: m_app.gotoNode("rabbitE"); break;
    case 0x15: m_app.gotoNode("rainbowS"); break;
    case 0x16: m_app.gotoNode("beachE"); break;
    default: break;
    }
}

void ToolbarNode::leaving(const std::string& nextNode) { m_app.selectNodeMusic(nextNode); } // 0x41e8d0

// --- anims ------------------------------------------------------------------------------------

engine::Anim* ToolbarNode::createAnim(std::string_view name, std::string_view instance) {
    return engine().anims().getOrCreate(name, instance);
}

engine::Anim* ToolbarNode::createAnimAtViewport(std::string_view name, std::string_view instance) {
    return getAnim(name, instance);
}

bool ToolbarNode::getCollision(engine::Anim* a, engine::Anim* b) {
    // 0x41d980: point samples, ending with a test of A at its own centre (a bug
    // that makes any opaque centre collide).
    if (!a || !b) {
        log::debug("GetCollision was passed a NULL Anim!");
        return false;
    }
    const float cx = cameraX(), cy = cameraY();
    auto hit = [&](engine::Anim* e, int x, int y) { return e->hitTest(x, y, cx, cy); };
    const engine::Rect rb = b->screenRect(cx, cy);
    if (hit(a, rb.left, rb.top) || hit(a, rb.left, rb.bottom) || hit(a, rb.right, rb.top) ||
        hit(a, rb.right, rb.bottom) || hit(a, (rb.left + rb.right) / 2, (rb.top + rb.bottom) / 2))
        return true;
    const engine::Rect ra = a->screenRect(cx, cy);
    if (hit(b, ra.left, ra.top) || hit(b, ra.left, ra.bottom) || hit(b, ra.right, ra.top) ||
        hit(b, ra.right, ra.bottom))
        return true;
    return hit(a, (ra.left + ra.right) / 2, (ra.top + ra.bottom) / 2);
}

bool ToolbarNode::getCollisionContains(engine::Anim* a, engine::Anim* b) {
    // 0x41db70.
    if (!a || !b) {
        log::debug("GetCollision was passed a NULL Anim!");
        return false;
    }
    const float cx = cameraX(), cy = cameraY();
    const engine::Rect rb = b->screenRect(cx, cy);
    auto hit = [&](int x, int y) { return a->hitTest(x, y, cx, cy); };
    return hit(rb.left, rb.top) && hit(rb.left, rb.bottom) && hit(rb.right, rb.top) && hit(rb.right, rb.bottom) &&
           hit((rb.left + rb.right) / 2, (rb.top + rb.bottom) / 2);
}

void ToolbarNode::frameEvent(engine::Anim& anim, int seq, int) {
    // 0x41c270: cue 999 on the sequence's last frame = "jump": move by the
    // frame's (dx, dy) and play another sequence. The record is stored in the
    // frame's anim-name field as {u16 seq, s16 dx, s16 dy}.
    if (seq < 0 || seq >= anim.sequenceCount())
        return;
    const auto& frames = anim.desc().sequences[static_cast<std::size_t>(seq)].frames;
    if (frames.empty() || frames.back().gotoSequence != 999 || frames.back().pauseAnim.size() < 6)
        return;
    const auto& d = frames.back().pauseAnim;
    auto s16 = [&](std::size_t i) {
        return static_cast<std::int16_t>(static_cast<unsigned char>(d[i]) | static_cast<unsigned char>(d[i + 1]) << 8);
    };
    anim.setPosition(anim.x() + s16(2), anim.y() + s16(4), anim.z());
    anim.setSequence(static_cast<std::uint16_t>(s16(0)), true);
}

// --- saddlebag ------------------------------------------------------------------------------------

void ToolbarNode::addBagItem(int item, bool quiet) {
    // 0x41d1a0.
    if (item == BarbieSaddlebag::kCoin)
        m_app.record().setU16(PlayerRecord::kHasCoin, 1);
    if (m_saddlebag)
        m_saddlebag->addItem(item, quiet);
    else
        log::warn("{}: AddBagItem({}) without a saddlebag", name(), item);
    engine().sounds().playSound("bagclose", -1, false, false, 1, false, 0x70, -1, 0, false);
}

bool ToolbarNode::hasBagItem(int item) const { return m_saddlebag && m_saddlebag->hasItem(item); }

void ToolbarNode::removeBagItem(int item) {
    // 0x41d220.
    if (item == BarbieSaddlebag::kCoin)
        m_app.record().setU16(PlayerRecord::kHasCoin, 0);
    if (m_saddlebag)
        m_saddlebag->removeItem(item);
}

// --- voice and sound ------------------------------------------------------------------------------

void ToolbarNode::runDelay(int ms) {
    // 0x41d250.
    delayMs = ms;
    runScript("Delay");
}

namespace {

// Voice ducking: music (head of channel 13) to `music`, the node's ambient loop to `ambient`.
void duck(engine::SoundManager& sounds, const std::string& ambientLoop, int music, int ambient) {
    if (engine::Sound* m = sounds.channelSound(13))
        m->setVolume(music);
    if (!ambientLoop.empty())
        if (engine::Sound* a = sounds.findLoaded(ambientLoop))
            a->setVolumePan(ambient, 0x40);
}

} // namespace

void ToolbarNode::playVoice(std::string_view name) {
    // 0x41d270.
    auto& sounds = engine().sounds();
    duck(sounds, m_ambient, 0x18, 0x20);
    sounds.playSound(name, 14, false, true, 1, false, 0x70, -1, 0, false);
    m_voicePlaying = true;
    updateMouthFlaps(true);
}

void ToolbarNode::playVoiceNoFlaps(std::string_view name) {
    // 0x41d330: queued behind the current line.
    auto& sounds = engine().sounds();
    duck(sounds, m_ambient, 0x18, 0x20);
    sounds.playSound(name, 14, true, false, 1, false, 0x70, -1, 0, false);
    m_voicePlaying = true;
}

void ToolbarNode::playVoiceAfterPause(std::string_view name) {
    // 0x41d310.
    playVoiceNoFlaps("pause");
    playVoiceNoFlaps(name);
}

void ToolbarNode::delayVoice(std::string_view name, int ms) {
    // 0x41d3c0.
    m_delayedVoice = name;
    m_delayedVoiceMs = ms;
    runScript("DelayVoice");
}

bool ToolbarNode::pollVoiceDone() {
    // 0x41d3f0.
    updateMouthFlaps(true);
    auto& sounds = engine().sounds();
    if (sounds.channelBusy(14))
        return false;
    duck(sounds, m_ambient, 0x30, 0x28);
    updateMouthFlaps(false);
    m_voicePlaying = false;
    return true;
}

void ToolbarNode::stopVoice() {
    // 0x41d480.
    auto& sounds = engine().sounds();
    if (sounds.channelBusy(14))
        sounds.stopChannel(14);
    duck(sounds, m_ambient, 0x30, 0x28);
    updateMouthFlaps(false);
    m_voicePlaying = false;
}

void ToolbarNode::updateMouthFlaps(bool on) {
    // 0x41d510: no lip-sync analysis; a flap anim matching Barbie's current
    // anim is shown while the voice channel is busy.
    auto& anims = engine().anims();
    if (!on) {
        if (m_flapsActive) {
            if (m_flaps)
                m_flaps->hide(true);
            m_flapsActive = false;
        }
        return;
    }
    auto showFlaps = [&](engine::Anim* f) {
        if (f && f->hideCount() != 0) {
            f->setHideCount(1);
            f->hide(false);
        }
        m_flapsActive = true;
    };
    if (engine::Anim* a = anims.find("ambientBarbie")) {
        if (!m_flapsActive) {
            if (a->sequence() == 2 && a->visible()) {
                m_flaps = getAnim("mountFlaps");
                if (m_flaps && name() == "yard")
                    m_flaps->setPosition(a->x(), a->y(), m_flaps->z());
                showFlaps(m_flaps);
                return;
            }
        } else if (a->sequence() != 2 || !a->visible()) {
            if (m_flaps && m_flaps == anims.find("mountFlaps")) {
                m_flaps->hide(true);
                m_flapsActive = false;
            }
        }
    }
    if (engine::Anim* b = anims.find("barbie ambients")) {
        const int seq = b->sequence();
        if (!m_flapsActive) {
            if (seq > 4 && seq < 8 && b->visible()) {
                m_flaps = getAnim("walkFlaps");
                if (m_flaps && m_flaps->hideCount() != 0) {
                    m_flaps->setHideCount(1);
                    m_flaps->hide(false);
                }
                if (m_flaps)
                    m_flaps->setPosition(b->x(), b->y(), b->z());
                m_flapsActive = true;
                return;
            }
        } else if (seq < 5 || seq > 7 || !b->visible()) {
            if (m_flaps && m_flaps == anims.find("walkFlaps")) {
                m_flaps->hide(true);
                m_flapsActive = false;
            }
        }
    }
    engine::Anim* g = anims.find("gameBarbie"); // (the original also looks up "BarbieIntro" and ignores it)
    if (!g)
        return;
    if (m_flapsActive) {
        if (g->sequence() == 0 && g->visible())
            return;
        if (m_flaps) {
            m_flaps->hide(true);
            m_flapsActive = false;
        }
        return;
    }
    if (g->sequence() != 0)
        return;
    m_flaps = getAnim("gameFlaps");
    if (m_flaps && name() == "bellgame")
        m_flaps->setPosition(g->x(), g->y(), m_flaps->z());
    showFlaps(m_flaps);
}

void ToolbarNode::stopMusic() { engine().sounds().stopChannel(13); }

void ToolbarNode::playAmbientLoop(std::string_view name, int volume) {
    // The scenes' ambient loops: PlaySound(name, -1, 0, 0, loop forever, keep, volume).
    engine().sounds().playSound(name, -1, false, false, 0, true, volume, -1, 0, false);
    m_ambient = name;
}

void ToolbarNode::releaseAmbient() {
    // 0x41d870.
    if (m_ambient.empty())
        return;
    engine().sounds().stopSound(m_ambient);
    engine().sounds().retain(m_ambient, false);
    m_ambient.clear();
}

std::string ToolbarNode::horseNameSound() const {
    // 0x41e670: "DB0%d" of the current horse's name (rec+0x18[horse]).
    const auto& rec = m_app.record();
    const std::size_t off = PlayerRecord::kHorseName + 4 * static_cast<std::size_t>(rec.horse());
    const int id = off + 4 <= PlayerRecord::kSize ? rec.s32(off) : 0;
    int n;
    if (id == 10)
        n = 2055;
    else if (id == 36)
        n = 2054;
    else if (id >= 37 && id <= 39)
        n = 668 + (id - 37);
    else if (id < 11)
        n = id + 631;
    else
        n = id + 632;
    return std::format("DB0{}", n);
}

void ToolbarNode::sayHorseName() { playVoiceNoFlaps(horseNameSound()); } // 0x41e720

void ToolbarNode::sayHorseNameThen(std::string_view name) {
    // 0x41d8b0.
    playVoiceNoFlaps(horseNameSound());
    playVoiceNoFlaps(name);
}

bool ToolbarNode::isVoiceChannelBusy() const {
    return const_cast<ToolbarNode*>(this)->engine().sounds().channelBusy(14);
}

void ToolbarNode::playHelpHint() {
    // 0x41e8f0: escalating help (rec+0x14C).
    auto& rec = m_app.record();
    const std::uint32_t n = rec.u32(PlayerRecord::kHelpHintCount);
    if (n == 0) {
        playVoiceNoFlaps("DB03006");
        playVoiceNoFlaps("DB9054a");
    } else if (n == 1) {
        playVoiceNoFlaps("DB9055a");
    } else {
        playVoiceNoFlaps("DB9056");
    }
    rec.setU32(PlayerRecord::kHelpHintCount, n + 1);
}

bool ToolbarNode::checkAllMissionsDone() {
    // 0x41e7e0.
    auto& rec = m_app.record();
    for (int i = 0; i < 8; ++i)
        if (rec.u32(PlayerRecord::kMissionDone + 4 * static_cast<std::size_t>(i)) == 0)
            return false;
    if (rec.u32(PlayerRecord::kFinaleState) != 0)
        return false;
    m_app.session().activeMission = 15;
    rec.setU32(PlayerRecord::kFinaleState, 1);
    playVoiceNoFlaps("pause");
    playVoiceNoFlaps("pause");
    playVoiceNoFlaps("DB04157");
    openPopup(popup::kSaddlebag);
    playVoiceNoFlaps("DB04158"); // its end goes to cottageL (soundDone)
    return true;
}

void ToolbarNode::choosePagerMessage() {
    // 0x41df10: a friend chat or a rescue offer for a badge not yet earned.
    if (!m_pager)
        return;
    auto& rec = m_app.record();
    if (engine().rand() % 2 != 0) {
        int friendId = engine().rand() % 2 != 0 ? 1 : 0;
        const std::uint32_t horse = rec.horse();
        if (horse == 1 && friendId == 0)
            friendId = 1;
        else if (horse == 0 && friendId == 1)
            friendId = 0;
        m_pager->pageMessage(friendId, friendId == 0 ? 0x3415 : 0x3416);
        return;
    }
    const bool r1 = rec.u32(PlayerRecord::kBadge + 4) != 0; // app+0x28C bird
    const bool r2 = rec.u32(PlayerRecord::kBadge + 0) != 0; // app+0x288 horseshoe
    const bool r3 = rec.u32(PlayerRecord::kBadge + 8) != 0; // app+0x290 branches
    switch (engine().rand() % 3) {
    case 0:
        if (!r1)
            return m_pager->pageMessage(0, 0x3417);
        if (!r2)
            return m_pager->pageMessage(1, 0x3419);
        if (!r3)
            return m_pager->pageMessage(1, 0x3418);
        break;
    case 1:
        if (!r2)
            return m_pager->pageMessage(1, 0x3419);
        if (!r3)
            return m_pager->pageMessage(1, 0x3418);
        if (!r1)
            return m_pager->pageMessage(0, 0x3417);
        return m_pager->pageMessage(1, 0x3419);
    case 2:
        if (!r3)
            return m_pager->pageMessage(1, 0x3418);
        if (r1)
            return m_pager->pageMessage(1, r2 ? 0x3418 : 0x3419);
        break;
    default:
        return m_pager->pageMessage(0, 0);
    }
    m_pager->pageMessage(0, 0x3417);
}

// --- sound and script hooks -------------------------------------------------------------------------

void ToolbarNode::soundDone(const std::string& sound, const void* id, bool completed) {
    // 0x41c460.
    if (str::iequals(sound, "DB04158")) {
        m_app.gotoNode("cottageL");
        engine::Node::soundDone(sound, id, completed);
        return;
    }
    if (rabbitLatch && str::iequals(sound, "DB9011")) {
        rabbitLatch = false;
        m_app.gotoNode("rabbitE");
    }
    engine::Node::soundDone(sound, id, completed);
}

void ToolbarNode::scriptEnded(engine::Script& script) {
    // 0x41dc80: after "DelayVoice", play the line it was waiting for.
    if (str::iequals(script.name(), "DelayVoice") && !m_delayedVoice.empty()) {
        playVoice(m_delayedVoice);
        m_delayedVoice.clear();
    }
    engine::Node::scriptEnded(script);
}

void ToolbarNode::scriptMessage(engine::Script& script, std::string_view message, int) {
    // 0x41dcd0.
    if (str::iequals(message, "initDelay")) {
        setScriptVar(script.name(), "delay", static_cast<double>(delayMs));
    } else if (str::iequals(message, "initDelayVoice")) {
        setScriptVar(script.name(), "delay", static_cast<double>(m_delayedVoiceMs));
    } else if (message.size() >= 6 && str::istartsWith(message, "voice ")) {
        playVoice(message.substr(6));
    } else if (str::istartsWith(message, "SayHor") && message.size() >= 6) {
        sayHorseName();
    }
}

} // namespace brc::game
