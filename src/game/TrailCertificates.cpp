// The trail certificates C1..C4 (0x43eca0–0x440580) and the riding lesson
// (0x445340–0x4457f0): TrailNode members the ride calls at fixed points (sound
// loading, stop setup and cleanup, each new frame, clicks, script ends, the
// camera). C1 (nature) and C2 (wildlife) are photo hunts with the toolbar
// camera, C3 (aid) is a photo of the rainbow, C4 (trail) is riding every
// trail. The lesson is the voice-over of the first ride on rabbitE. See
// docs/engine/nodes.md "TrailNode mission, certificate and rescue code".
#include "game/TrailNode.h"

#include "core/Log.h"
#include "core/StringUtil.h"
#include "engine/Anim.h"
#include "engine/Engine.h"
#include "engine/SoundManager.h"
#include "game/PlayerRecord.h"
#include "game/TrailFlags.h"
#include "game/TrailTables.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace brc::game {
namespace {

// Bag items of the photo popups and the rainbow photo.
constexpr int kRosePhoto = 0x10, kTulipPhoto = 0x11, kDaisyPhoto = 0x12;
constexpr int kOwlPhoto = 0x13, kHawkPhoto = 0x14, kBluebirdPhoto = 0x15, kRainbowPhoto = 0x16;
constexpr int kAnimalClick = 0x2724;       // the C2 owl, hawk or bluebird
constexpr int kCertificate3Click = 0x2725; // the C3 certificate

// Record fields (app+0x278.. and app+0x2AE..).
constexpr std::size_t kNatureCount = PlayerRecord::kCertificate;         // app+0x278
constexpr std::size_t kWildlifeCount = PlayerRecord::kCertificate + 4;   // app+0x27C
constexpr std::size_t kAidCount = PlayerRecord::kCertificate + 8;        // app+0x280
constexpr std::size_t kTrailCount = PlayerRecord::kCertificate + 12;     // app+0x284
constexpr std::size_t kRosePhotoTaken = PlayerRecord::kC1Photos;         // app+0x2AE
constexpr std::size_t kTulipPhotoTaken = PlayerRecord::kC1Photos + 2;    // app+0x2B0
constexpr std::size_t kDaisyPhotoTaken = PlayerRecord::kC1Photos + 4;    // app+0x2B2
constexpr std::size_t kOwlPhotoTaken = PlayerRecord::kC2Photos;          // app+0x2B4
constexpr std::size_t kBluebirdPhotoTaken = PlayerRecord::kC2Photos + 2; // app+0x2B6
constexpr std::size_t kHawkPhotoTaken = PlayerRecord::kC2Photos + 4;     // app+0x2B8

bool allFlowerPhotos(const PlayerRecord& r) {
    return r.u16(kRosePhotoTaken) != 0 && r.u16(kTulipPhotoTaken) != 0 && r.u16(kDaisyPhotoTaken) != 0;
}

bool allAnimalPhotos(const PlayerRecord& r) {
    return r.u16(kHawkPhotoTaken) != 0 && r.u16(kOwlPhotoTaken) != 0 && r.u16(kBluebirdPhotoTaken) != 0;
}

void increment(PlayerRecord& r, std::size_t off) {
    r.setU32(off, r.u32(off) + 1);
}

int s_c1HintTicks = 0;          // [0x80a268]: rides ticks on a trail without flowers
bool s_wildlifeAwarded = false; // [0x80a26c]
int s_c4Groups = 0;             // [0x80a274]: trail groups ridden, as last announced

// The lesson voices by control mode (app+0x1A8: 0 mouse buttons, 1 keyboard),
// in pairs at 0x804b40.
constexpr std::array<std::string_view, 12> kLessonVoices = {"db0448", "db0443", "db0447",  "db0442",
                                                            "db0451", "db0445", "db0449",  "db0444",
                                                            "db0452", "db0446", "db02036", "db0441"};
enum LessonVoice { kL0448 = 0, kL0447 = 2, kL0451 = 4, kL0449 = 6, kL0452 = 8, kL02036 = 10 };

// LessonRideTick's statics ([0x80a324]..[0x80a364]): each line is said once
// per ride along the trail.
struct LessonState {
    bool trailInit = false; // [0x80a324] bit 0
    int trail = 0;          // [0x80a328]
    bool start = false;     // [0x80a32c] db0447 after frame 5
    bool steer = false;     // [0x80a330] db0448 after 0x3C
    bool speed = false;     // [0x80a334] db0452 after 0x122
    bool gait = false;      // [0x80a338] db02036 after 0xA0
    bool jump1 = false;     // [0x80a33c] db0451 before the first obstacle
    bool stop = false;      // [0x80a340] db0449 after 400
    bool end = false;       // [0x80a344] db0453 after 0x2A3 (never re-armed)
    bool start2 = false;    // [0x80a348] db0447 after 0x1C2
    bool steer2 = false;    // [0x80a34c] db0448 after 500
    bool speed2 = false;    // [0x80a350] db0452 after 600
    bool gait2 = false;     // [0x80a354] db02036 after 0x226
    bool jump2 = false;     // [0x80a358] db0451 before the second obstacle
    bool stop2 = false;     // [0x80a35c] db0449 after 0x28A
    bool jump3 = false;     // [0x80a360] db0451 before the third obstacle
    bool finished = false;  // [0x80a364] the horse stops after 0x2D5
};
LessonState s_lesson;

} // namespace

// --- C1: the nature certificate (roses, tulips, daisies) ---------------------------------------

void TrailNode::c1LoadSounds() {
    // C1_LoadSounds (0x43eca0).
    auto& snd = engine().sounds();
    snd.getSound("db0203", true, false);
    snd.getSound("db9027", true, false);
}

void TrailNode::c1ReleaseSounds() {
    // C1_ReleaseSounds (0x43ecd0).
    auto& snd = engine().sounds();
    snd.retain("db0203", false);
    snd.retain("db9027", false);
}

void TrailNode::c1SetupStop() {
    // C1_SetupStop (0x43ed00): the flowers of this trail, if the horse stopped
    // in their frame window.
    m_cert.natureAwarded = false;
    m_missionTrail = m_mapLocation;
    m_cert.flower = nullptr;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation == 0) {
        if (f > 0x220 && f < 0x28B) {
            m_cert.flower = createAnimAtViewport("aat roses");
            playVoice("db0209");
        }
    } else if (m_mapLocation == 2) {
        if ((f > 0x27 && f < 0x88) || (f > 0xA9 && f < 0x119)) {
            m_cert.flower = createAnimAtViewport("obt tulips");
            playVoice("db0207");
            if (engine::Anim* rail = engine().anims().find("bridge rail"))
                rail->hide(true);
        }
    } else if (m_mapLocation == 3 && f > 0x68 && f < 0x15F) {
        m_cert.flower = createAnimAtViewport("rft daisies");
        playVoice("db0208");
    }
}

void TrailNode::c1CleanupStop() {
    // C1_CleanupStop (0x43ee10): the flowers go (when still on the trail they
    // came with) and the hints of the photos not taken are said again.
    if (m_mapLocation == m_missionTrail && m_cert.flower)
        deleteAnim(m_cert.flower);
    const auto& rec = app().record();
    if (rec.u16(kDaisyPhotoTaken) == 0)
        m_cert.daisyHint = false;
    if (rec.u16(kRosePhotoTaken) == 0)
        m_cert.roseHint = false;
    if (rec.u16(kTulipPhotoTaken) == 0)
        m_cert.tulipHint = false;
    auto& anims = engine().anims();
    if (anims.find("picUpClose"))
        anims.remove("picUpClose");
    m_pickup = nullptr;
}

void TrailNode::c1RideTick() {
    // C1_RideTick (0x43eeb0): on the flower trails, db0203 once along the way;
    // on the others, db9027 ("no flowers here") after 100 ticks, once per trail.
    switch (m_mapLocation) {
    case 0:
        if (!m_cert.roseHint && static_cast<std::uint32_t>(trailFrame()) > 0x230) {
            playVoice("db0203");
            m_cert.roseHint = true;
        }
        break;
    case 1:
    case 4:
    case 5:
    case 6:
        if (m_hintTrail != m_mapLocation) {
            if (s_c1HintTicks == 100) {
                playVoiceNoFlaps("db9027");
                s_c1HintTicks = 0;
                m_hintTrail = m_mapLocation;
            } else {
                ++s_c1HintTicks;
            }
        }
        break;
    case 2:
        if (!m_cert.tulipHint && static_cast<std::uint32_t>(trailFrame()) > 0xBE) {
            playVoice("db0203");
            m_cert.tulipHint = true;
        }
        break;
    case 3:
        if (!m_cert.daisyHint && static_cast<std::uint32_t>(trailFrame()) > 200) {
            playVoice("db0203");
            m_cert.daisyHint = true;
        }
        break;
    default:
        break;
    }
}

void TrailNode::c1MouseDown(const engine::Pick& pick) {
    // C1_MouseDown (0x43efc0): closing a photo popup with all three flowers
    // photographed awards the certificate.
    if (!pick.element || pick.element->clickId() != kPickupClick)
        return;
    auto& rec = app().record();
    if (!allFlowerPhotos(rec) || m_cert.natureAwarded)
        return;
    m_cert.natureAwarded = true;
    increment(rec, kNatureCount);
    auto& s = app().session();
    if (s.activeMission == 8) {
        s.activeMission = 0x11;
        s.missionTrail = 7;
    }
    app().saveGame();
    m_certificate = createAnimAtViewport("certificatewon_nature");
    showCertificateName();
    if (m_certificate)
        m_certificate->setClickId(kCertificateClick);
    playVoice("db0211");
}

void TrailNode::c1CameraClick() {
    // C1_CameraClick (0x43f0b0, ListenToMessage 0x32CD): a photo of the
    // flowers shows it in a popup (the bag item when it is clicked away).
    // Without flowers the toolbar takes an ordinary photo.
    if (!m_cert.flower) {
        skipNextPhoto = false;
        ToolbarNode::listen(kMsgCamera, {});
        return;
    }
    auto& rec = app().record();
    auto showPhoto = [&](std::string_view popup, int item, std::size_t flag) {
        m_pickup = createAnimAtViewport(popup, "picUpClose");
        if (m_pickup) {
            m_pickup->setClickId(kPickupClick);
            m_pickup->setSequence(1, true);
        }
        m_pickupItem = item;
        rec.setU16(flag, 1);
    };
    // The count goes up on every photo once all three are taken (it is only
    // ever tested for nonzero); the tulips count before their own flag is set.
    switch (m_mapLocation) {
    case 0:
        if (!m_pickup)
            showPhoto("c1 rosePicOut", kRosePhoto, kRosePhotoTaken);
        break;
    case 2:
        if (allFlowerPhotos(rec))
            increment(rec, kNatureCount);
        if (m_pickup)
            return;
        showPhoto("c1 tulipPicOut", kTulipPhoto, kTulipPhotoTaken);
        return;
    case 3:
        if (!m_pickup) {
            showPhoto("c1 daisyPicOut", kDaisyPhoto, kDaisyPhotoTaken);
            if (allFlowerPhotos(rec))
                increment(rec, kNatureCount);
            return;
        }
        break;
    default:
        return;
    }
    if (allFlowerPhotos(rec))
        increment(rec, kNatureCount);
}

// --- C2: the wildlife certificate (owl, hawk, bluebird) ----------------------------------------

void TrailNode::c2SetupStop() {
    // C2_SetupStop (0x43f2d0): the owl (appleW stop 2), the hawk (rabbitE) or
    // the bluebird (beachE) becomes the photo target, by the frame of the
    // movie playing (the trail or an insert; none during a refusal loop).
    m_cert.animal = nullptr;
    m_missionTrail = m_mapLocation;
    s_wildlifeAwarded = false;
    std::uint32_t f = 0;
    if (m_current && m_current == m_base.get())
        f = m_base->frameNum();
    else if (m_current && m_current == m_insert.get())
        f = m_insert->frameNum();
    else
        return;
    if (m_mapLocation == 0) {
        if (m_decision == 2) {
            m_skipStopComment = true;
            m_cert.animal = m_owl;
            if (m_owl)
                m_owl->setClickId(kAnimalClick);
            playVoice("db0226");
        }
    } else if (m_mapLocation == 4) {
        if (m_decision == 5 && f > 0xD2 && f < 0x14A) {
            m_cert.animal = createAnimAtViewport("c2hawkanim");
            if (m_cert.animal)
                m_cert.animal->setClickId(kAnimalClick);
            m_skipStopComment = true;
            playVoice("db0222");
        }
    } else if (m_mapLocation == 5 && m_decision == 5 && f > 0x96 && f < 0x136) {
        m_skipStopComment = true;
        m_cert.animal = createAnimAtViewport("c2bbirdanim");
        if (m_cert.animal) {
            m_cert.animal->setClickId(kAnimalClick);
            m_cert.animal->setSequence(0, true);
        }
        delayVoice("db0230", 2000);
    }
}

void TrailNode::c2CleanupStop() {
    // C2_CleanupStop (0x43f460): the hawk or the bluebird goes (the owl is a
    // stop prop, already gone with the stop's props); hints are re-armed for
    // the photos not taken.
    if (m_mapLocation == m_missionTrail && m_cert.animal && m_missionTrail != 0)
        deleteAnim(m_cert.animal);
    else if (m_missionTrail == 0)
        m_cert.animal = nullptr; // the owl, deleted by ClearStopProps (the original keeps the stale pointer)
    auto& anims = engine().anims();
    if (anims.find("picUpClose"))
        anims.remove("picUpClose");
    m_pickup = nullptr;
    const auto& rec = app().record();
    if (rec.u16(kOwlPhotoTaken) == 0)
        m_cert.owlHint = false;
    if (rec.u16(kHawkPhotoTaken) == 0)
        m_cert.hawkHint = false;
    if (rec.u16(kBluebirdPhotoTaken) == 0)
        m_cert.bluebirdHint = false;
}

void TrailNode::c2ScriptEnded(std::string_view script) {
    // C2_ScriptEnded (0x43f510): on beachE the bluebird shows a moment after
    // the horse has stopped.
    if (m_mapLocation != 5)
        return;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (str::iequals(script, "runOn") && m_decision == 5) {
        runDelay(500);
        return;
    }
    if (str::iequals(script, "delay") && m_decision == 5 && f > 0x96 && f < 200)
        forceShowAnim(m_cert.animal);
}

void TrailNode::c2MouseDown(const engine::Pick& pick) {
    // C2_MouseDown (0x43f5a0): closing a photo popup with all three animals
    // photographed awards the certificate.
    if (!pick.element || pick.element->clickId() != kPickupClick)
        return;
    auto& rec = app().record();
    if (!allAnimalPhotos(rec) || s_wildlifeAwarded)
        return;
    s_wildlifeAwarded = true;
    increment(rec, kWildlifeCount);
    auto& s = app().session();
    if (s.activeMission == 9) {
        s.activeMission = 0x11;
        s.missionTrail = 7;
    }
    app().saveGame();
    m_certificate = createAnimAtViewport("certificatewon_wildlife");
    showCertificateName();
    if (m_certificate)
        m_certificate->setClickId(kCertificateClick);
    playVoice("db0234");
    playVoiceNoFlaps("db0197");
    playVoiceNoFlaps("db0198");
}

void TrailNode::c2CameraClick() {
    // C2_CameraClick (0x43f6b0, ListenToMessage 0x32CD): a photo of the animal
    // while it sits still (sequence 0); otherwise an ordinary photo.
    if (!m_cert.animal || m_cert.animal->sequence() != 0) {
        skipNextPhoto = false;
        ToolbarNode::listen(kMsgCamera, {});
        return;
    }
    auto& rec = app().record();
    auto showPhoto = [&](std::string_view popup, int item, std::size_t flag) {
        m_pickup = createAnimAtViewport(popup, "picUpClose");
        if (m_pickup) {
            m_pickup->setClickId(kPickupClick);
            m_pickup->setSequence(1, true);
        }
        m_pickupItem = item;
        rec.setU16(flag, 1);
    };
    // As with C1, every photo after the third counts again.
    switch (m_mapLocation) {
    case 0:
        if (!m_pickup)
            showPhoto("c2 owlpicOut", kOwlPhoto, kOwlPhotoTaken);
        break;
    case 4:
        if (!m_pickup) {
            showPhoto("c2 hawkpicOut", kHawkPhoto, kHawkPhotoTaken);
            if (allAnimalPhotos(rec))
                increment(rec, kWildlifeCount);
            return;
        }
        break;
    case 5:
        if (!m_pickup) {
            showPhoto("c2 bbirdpicOut", kBluebirdPhoto, kBluebirdPhotoTaken);
            if (allAnimalPhotos(rec))
                increment(rec, kWildlifeCount);
            return;
        }
        break;
    default:
        return;
    }
    if (allAnimalPhotos(rec))
        increment(rec, kWildlifeCount);
}

void TrailNode::c2RideTick() {
    // C2_RideTick (0x43f8e0): the animal's call and Barbie's cue, once per
    // stop window.
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation == 0) {
        if (!m_cert.owlHint && f > 0x65 && f < 0xB0) {
            playVoice("owl");
            playVoiceNoFlaps("db0223");
            m_cert.owlHint = true;
        }
    } else if (m_mapLocation == 4) {
        if (!m_cert.hawkHint && f > 0xD2 && f < 300) {
            playVoice("hawk");
            playVoiceNoFlaps("hawk");
            playVoiceNoFlaps("db0218");
            m_cert.hawkHint = true;
        }
    } else if (m_mapLocation == 5 && !m_cert.bluebirdHint && f > 0x96 && f < 200) {
        playVoice("bluebird");
        playVoiceNoFlaps("db0227");
        m_cert.bluebirdHint = true;
    }
}

namespace {
constexpr std::array<std::string_view, 6> kC2Sounds = {"owl",    "db0223",   "hawk",
                                                       "db0218", "bluebird", "db0227"};
} // namespace

void TrailNode::c2LoadSounds() {
    // C2_LoadSounds (0x43f9d0).
    for (auto v : kC2Sounds)
        engine().sounds().getSound(v, true, false);
}

void TrailNode::c2ReleaseSounds() {
    // C2_ReleaseSounds (0x43fa50).
    for (auto v : kC2Sounds)
        engine().sounds().retain(v, false);
}

// --- C3: the aid certificate (the rainbow) -------------------------------------------------------

void TrailNode::c3SetupStop() {
    // C3_SetupStop (0x43fad0): at the rainbow (rainbowS stop 0), Barbie asks
    // for a photo.
    m_missionTrail = m_mapLocation;
    m_cert.certificate3 = nullptr;
    if (m_decision == 0 && m_mapLocation == 3) {
        playVoiceNoFlaps("spause");
        playVoiceNoFlaps("DB0192");
        playVoiceNoFlaps("spause");
        playVoiceNoFlaps("DB04085");
    }
}

void TrailNode::c3CleanupStop() {
    // C3_CleanupStop (0x43fb30).
    if (m_cert.certificate3)
        deleteAnim(m_cert.certificate3);
    m_cert.certificate3 = nullptr;
}

void TrailNode::c3MouseDown(const engine::Pick& pick) {
    // C3_MouseDown (0x43fb60): clicking the rainbow certificate awards it.
    if (m_decision != 0 || !pick.element || pick.element->clickId() != kCertificate3Click)
        return;
    playVoice("DB04086");
    auto& s = app().session();
    if (s.activeMission == 10) {
        s.activeMission = 0x11;
        s.missionTrail = 7;
    }
    app().saveGame();
    m_certificate = createAnimAtViewport("certificatewon_aid");
    showCertificateName();
    if (m_certificate)
        m_certificate->setClickId(kCertificateClick);
    if (m_cert.certificate3)
        m_cert.certificate3->hide(true);
}

void TrailNode::c3CameraClick() {
    // C3_CameraClick (0x43fc20, ListenToMessage 0x32CD): the photo of the
    // rainbow is the certificate (the count, a save and the photo in the bag),
    // then the toolbar goes on as usual. The function also has branches for
    // the messages 0x2714 (DB04088) and 0x2725 (DB04087 and `certificate3Out`)
    // that are never taken: it is only called with 0x32CD.
    if (m_decision == 0) {
        m_cert.certificate3 = createAnimAtViewport("Certificate3");
        if (m_cert.certificate3) {
            m_cert.certificate3->setPosition(m_cert.certificate3->x(), m_cert.certificate3->y(), 1000.0f);
            m_cert.certificate3->setClickId(kCertificate3Click);
        }
        auto& rec = app().record();
        increment(rec, kAidCount);
        app().saveGame();
        addBagItem(kRainbowPhoto);
        playVoiceNoFlaps("DB0195");
    }
    ToolbarNode::listen(kMsgCamera, {});
}

void TrailNode::c3RideTick() {
    // C3_RideTick (0x43fd50): db04084 approaching the rainbow.
    if (m_mapLocation != static_cast<int>(app().session().missionTrail))
        return;
    const auto f = static_cast<std::uint32_t>(trailFrame());
    if (m_mapLocation == 3 && !m_cert.rainbowHint && f > 0x41 && f < 0x73) {
        playVoice("db04084");
        m_cert.rainbowHint = true;
    }
}

void TrailNode::c3LoadSounds() {
    // C3_LoadSounds (0x43fdb0).
    engine().sounds().getSound("db04084", true, false);
}

void TrailNode::c3ReleaseSounds() {
    // C3_ReleaseSounds (0x43fdd0).
    engine().sounds().retain("db04084", false);
}

// --- C4: the trail certificate (ride every trail) ----------------------------------------------

namespace {
constexpr std::array<std::string_view, 5> kC4Sounds = {"db0241", "db0242", "db0243", "db0245", "db0248"};
} // namespace

void TrailNode::c4LoadSounds() {
    // C4_LoadSounds (0x43fdf0).
    for (auto v : kC4Sounds)
        engine().sounds().getSound(v, true, false);
}

void TrailNode::c4ReleaseSounds() {
    // C4_ReleaseSounds (0x43fe60).
    for (auto v : kC4Sounds)
        engine().sounds().retain(v, false);
}

void TrailNode::c4SetupStop() {
    // C4_SetupStop (0x43fec0): the certificate earned on the last trail.
    auto& s = app().session();
    if (s.trailCertPending != 1)
        return;
    s.trailCertPending = 0;
    m_certificate = createAnimAtViewport("certificatewon_trail");
    showCertificateName();
    if (m_certificate)
        m_certificate->setClickId(kCertificateClick);
}

void TrailNode::c4UpdateProgress() {
    // C4_UpdateProgress (0x43ff10): the trail groups ridden (appleW in two
    // parts, rainbowS, rabbitE in two parts, bridgeS, beachE); a voice for
    // each new count, the certificate at five.
    auto& s = app().session();
    const auto& p = s.c4Progress; // app+0x322..+0x32E
    int groups = 0;
    if (p[0] != 0 && p[1] != 0)
        groups = 1;
    if (p[2] != 0)
        ++groups;
    if (p[3] != 0 && p[4] != 0)
        ++groups;
    if (p[5] != 0)
        ++groups;
    if (p[6] != 0)
        ++groups;
    if (groups != s_c4Groups) {
        switch (groups) {
        case 1:
            playVoiceNoFlaps("db0241");
            s_c4Groups = groups;
            return;
        case 2:
            playVoiceNoFlaps("db0248");
            s_c4Groups = groups;
            return;
        case 3:
            playVoiceNoFlaps("db0242");
            s_c4Groups = groups;
            return;
        case 4:
            playVoiceNoFlaps("db0243");
            s_c4Groups = groups;
            return;
        case 5: {
            playVoiceNoFlaps("db0245");
            auto& rec = app().record();
            increment(rec, kTrailCount);
            s.trailCertPending = 1;
            app().saveGame();
            break;
        }
        default:
            break;
        }
    }
    s_c4Groups = groups;
}

void TrailNode::c4LoadTrail(int trailEnded) {
    // C4LoadTrail (0x440040), from LoadTrail (0) and TrailEnded on appleW (1):
    // riding from one trail into the next marks the group of the trail last
    // started (app+0x330) as ridden; then the trail being loaded becomes the
    // one last started, by where it starts.
    auto& s = app().session();
    auto& p = s.c4Progress;
    bool update = false;
    switch (s.c4LastTrail) {
    case 0: // appleW from its start
        if (m_trail == 4 || m_trail == 5) {
            p[0] = 1;
            p[1] = 1;
        } else if (m_trail == 0 && trailEnded != 0) {
            p[0] = 1;
            p[1] = 1;
            s.c4LastTrail = 7;
            update = true;
        }
        break;
    case 1: // appleW from its middle
        if (m_trail == 4 || m_trail == 5) {
            p[1] = 1;
        } else if (m_trail == 0 && trailEnded != 0) {
            p[1] = 1;
            s.c4LastTrail = 7;
            update = true;
        }
        break;
    case 2: // rainbowS
        if (m_trail == 0)
            p[2] = 1;
        break;
    case 3: // rabbitE from its start
        if (m_trail == 2) {
            p[3] = 1;
        } else if (m_trail == 3) {
            p[3] = 1;
            p[4] = 1;
        }
        break;
    case 4: // rabbitE from its middle
        if (m_trail == 3)
            p[4] = 1;
        break;
    case 5: // bridgeS
        if (m_trail == 0)
            p[5] = 1;
        break;
    case 6: // beachE
        if (m_trail == 0)
            p[6] = 1;
        break;
    case 7:
        break;
    default:
        log::debug("TrailNode::C4LoadTrail() - unhandled lastTrailStarted");
        break;
    }
    if (!update) {
        switch (m_trail) {
        case 0:
            if (m_startFrame == 0 || m_startFrame == 0x38)
                s.c4LastTrail = 0;
            else if (m_startFrame > 0x159 && m_startFrame < 0x16E)
                s.c4LastTrail = 1;
            break;
        case 1:
        case 6:
        case 7:
            break;
        case 2:
            s.c4LastTrail = 5;
            break;
        case 3:
            s.c4LastTrail = 2;
            break;
        case 4:
            if (m_startFrame == 0 || m_startFrame == 0x447)
                s.c4LastTrail = 3;
            else if (m_startFrame > 0x30C && m_startFrame < 800)
                s.c4LastTrail = 4;
            break;
        case 5:
            s.c4LastTrail = 6;
            break;
        default:
            log::debug("TrailNode::C4LoadTrail() - unhandled trail");
            break;
        }
    }
    c4UpdateProgress();
}

void TrailNode::c4UnloadTrail() {
    // C4UnloadTrail (0x440340), from the destructor: leaving within 25 frames
    // of the end of the trail last started counts it as ridden.
    auto& s = app().session();
    auto& p = s.c4Progress;
    auto nearEnd = [&] {
        return m_base && static_cast<int>(m_base->frames()) - static_cast<int>(m_base->frameNum()) < 0x19;
    };
    switch (s.c4LastTrail) {
    case 0:
        if (m_trail == 0 && m_base) {
            if (nearEnd()) {
                p[0] = 1;
                p[1] = 1;
            } else if (static_cast<int>(m_base->frameNum()) > 0x159) {
                p[0] = 1;
            }
        }
        break;
    case 1:
        if (nearEnd())
            p[1] = 1;
        break;
    case 2:
        if (nearEnd())
            p[2] = 1;
        break;
    case 3:
        if (nearEnd()) {
            p[3] = 1;
            p[4] = 1;
        } else if (m_base && static_cast<int>(m_base->frameNum()) > 0x30C) {
            p[3] = 1;
        }
        break;
    case 4:
        if (nearEnd())
            p[4] = 1;
        break;
    case 5:
        if (nearEnd())
            p[5] = 1;
        break;
    case 6:
        if (nearEnd())
            p[6] = 1;
        break;
    case 7:
        break;
    default:
        log::debug("TrailNode::C4LoadTrail() - unhandled lastTrailStarted");
        break;
    }
    s.c4LastTrail = 7;
    c4UpdateProgress();
}

// --- the riding lesson -------------------------------------------------------------------------

void TrailNode::lessonLoadSounds() {
    // LessonLoadSounds (0x445340), from the constructor of the first ride:
    // the lines of the control mode. (With the mouse-motion mode, 2, the
    // original reads the table one pair off and past its end; only the
    // entries inside it are loaded here.)
    auto& snd = engine().sounds();
    const auto mode = static_cast<std::size_t>(app().controlMode);
    for (std::size_t i = 0; i < 0xB; i += 2)
        if (mode + i < kLessonVoices.size())
            snd.getSound(kLessonVoices[mode + i], true, false);
    snd.getSound("db0453", true, false);
    snd.getSound("db0457", true, false);
    snd.getSound("db0458", true, false);
}

void TrailNode::lessonReleaseSounds() {
    // LessonReleaseSounds (0x4453b0), from the destructor.
    auto& snd = engine().sounds();
    const auto mode = static_cast<std::size_t>(app().controlMode);
    for (std::size_t i = 0; i < 0xB; i += 2)
        if (mode + i < kLessonVoices.size())
            snd.retain(kLessonVoices[mode + i], false);
    snd.retain("db0453", false);
    snd.retain("db0457", false);
    snd.retain("db0458", false);
}

void TrailNode::lessonRideTick() {
    // LessonRideTick (0x445420): the lesson's lines at fixed frames of rabbitE
    // (and before each obstacle), one per call, then the horse stops at the
    // end of the lesson. Nothing in the mouse-motion mode.
    const int mode = static_cast<int>(app().controlMode);
    if (mode == 2 || m_trail != 4 || !trailflags::dialogue || voicePlaying())
        return;
    auto& l = s_lesson;
    if (!l.trailInit) {
        l.trailInit = true;
        l.trail = m_trail;
    }
    if (!m_base || m_base->paused)
        return;
    const int f = static_cast<int>(m_base->frameNum());
    if (l.trail != m_trail || (f < 5 && l.start)) {
        const bool end = l.end; // (not re-armed)
        l = LessonState{};
        l.trailInit = true;
        l.end = end;
        l.trail = m_trail;
    }
    auto say = [&](int voice) { playVoiceNoFlaps(kLessonVoices[static_cast<std::size_t>(voice + mode)]); };
    auto beforeObstacle = [&](int i) {
        const int end = trail::kObstacleEnd[static_cast<std::size_t>(m_trail * 3 + i)];
        return end != 0 && end - 0x28 < f;
    };
    if (!l.start && f > 5) {
        say(kL0447);
        l.start = true;
    } else if (!l.steer && f > 0x3C) {
        say(kL0448);
        l.steer = true;
    } else if (!l.jump1 && beforeObstacle(0)) {
        say(kL0451);
        l.jump1 = true;
    } else if (!l.gait && f > 0xA0) {
        say(kL02036);
        l.gait = true;
    } else if (!l.jump2 && beforeObstacle(1)) {
        say(kL0451);
        l.jump2 = true;
    } else if (!l.speed && f > 0x122) {
        say(kL0452);
        l.speed = true;
    } else if (!l.jump3 && beforeObstacle(2)) {
        say(kL0451);
        l.jump3 = true;
    } else if (!l.stop && f > 400) {
        say(kL0449);
        l.stop = true;
    } else if (!l.start2 && f > 0x1C2) {
        say(kL0447);
        l.start2 = true;
    } else if (!l.steer2 && f > 500) {
        say(kL0448);
        l.steer2 = true;
    } else if (!l.gait2 && f > 0x226) {
        say(kL02036);
        l.gait2 = true;
    } else if (!l.speed2 && f > 600) {
        say(kL0452);
        l.speed2 = true;
    } else if (!l.stop2 && f > 0x28A) {
        say(kL0449);
        l.stop2 = true;
    } else if (!l.end && f > 0x2A3) {
        playVoiceNoFlaps("db0453");
        l.end = true;
    } else if (!l.finished && f > 0x2D5) {
        horseStop();
        m_tutorialFinished = true;
        l.finished = true;
    }
}

void TrailNode::lessonIdleHint() {
    // LessonIdleHint (0x4457f0), every 30 s without input during the lesson.
    if (trailflags::dialogue && m_trail == 4)
        playVoiceNoFlaps(
            kLessonVoices[static_cast<std::size_t>(kL0451 + static_cast<int>(app().controlMode))]);
}

} // namespace brc::game
