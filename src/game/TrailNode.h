#pragma once

#include "game/SmackOverlayMovie.h"
#include "game/ToolbarNode.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace brc::engine {
class Anim;
class Font;
class Script;
class Sound;
class TextElement;
} // namespace brc::engine

namespace brc::game {

class ACRollover;

// TrailNode (TrailNode.cpp 0x42fda0–0x434caf, TrailRide.cpp 0x434cb0–0x43c0bf;
// ctor 0x431420, size 0x7FC): the first-person trail rides. A Smacker trail
// movie (SmackOverlayMovie) shown doubled and panned by the steering, with a
// horse-head overlay that turns and jumps, obstacles, forks that switch
// between the eight trail movies inside the node, and stops where Barbie can
// dismount, walk, feed the horse and click the props of the missions. The
// seven trail locations (appleW … wishingL) are thin subclasses that pick the
// starting trail. See docs/engine/nodes.md "TrailNode" and movies.md §4.
//
// Field comments give the original node offsets. The mission, certificate and
// rescue code (0x43eca0–0x4457f0) is not ported yet: its entry points are the
// m*/c*/r*/lesson* members (TrailMissionStubs.cpp).
class TrailNode : public ToolbarNode {
public:
    // Trail ids (+0x400) and their movies: 0 aaW AppleW, 1 cwL CottageW,
    // 2 obS Bridge, 3 rfS Rainbow, 4 rrE RabbitE, 5 trE TurtleE, 6 wwE
    // WishingN, 7 wwW WishingS.
    static constexpr int kTrails = 8;
    // Horse states (+0x440, SetHorseState 0x43b240; names at 0x8038f8).
    enum HorseState {
        kMountRun = 0, kAmbient = 1, kTurnLeft = 2, kTurnRight = 3, kLookLeft = 4, kExcitedLookLeft = 5,
        kSubtleAmbient = 6, kGeneralAmbient = 7, kWalk = 8, kWalkAmbient = 9, kTenderMoment = 10,
        kFeedApple = 11, kHideAll = 12, kNoState = 13
    };
    // Click ids (anim +0x11C) of the trail's own props.
    static constexpr int kHorseClick = 10000;      // horse and Barbie: ToggleMount
    static constexpr int kExitPropClick = 0x2711;  // mini-game entrance set by the missions
    static constexpr int kRoadSignClick = 0x2712;
    static constexpr int kMailboxClick = 0x2713;
    static constexpr int kPickupClick = 0x2714;
    static constexpr int kCertificateClick = 0x2715;
    static constexpr int kCritterClick = 0x2716;
    static constexpr int kOwlClick = 0x2717;
    static constexpr int kCowClick = 0x2718;
    static constexpr int kBushClick = 0x271F;
    static constexpr int kWellClick = 0x2721;

    TrailNode(Godiva& app, const std::string& name, int trail); // 0x431420
    ~TrailNode() override;                                      // 0x431fa0

    // --- engine hooks -------------------------------------------------------------
    void start() override;                                   // +0x48 (0x431f80)
    void tick() override;                                    // SpendTime (0x435980)
    void onPress(const engine::Pick& pick) override;         // +0x54 MouseDown (0x432600)
    void onKeyDown(const engine::InputEvent& e) override;    // +0x6C (0x434f60)
    void onKeyUp(const engine::InputEvent& e) override;      // +0x70 (0x435400)
    void frameEvent(engine::Anim& anim, int seq, int index) override; // +0x7C AnimFrameCue (0x43b180)
    void scriptStarted(const std::string& name) override;    // +0x88 (0x43aa40)
    void scriptPreempted(engine::Script& script) override;   // +0x8C (0x43b000)
    void scriptEnded(engine::Script& script) override;       // +0x90 (0x43aab0)
    void scriptMessage(engine::Script& script, std::string_view message, int counter) override; // +0x98 (0x43a080)
    void listen(int message, const engine::MessageData& data) override; // ListenToMessage (0x435510)
    void gotoNodeById(int id) override;                      // +0xD8 (0x439e20): the map

    // --- TrailNode.cpp -----------------------------------------------------------------
    void spawnCritter();                      // 0x42fda0
    void removeCritter();                     // 0x430250
    void critterClicked();                    // 0x4302a0
    void sayCritterComment();                 // 0x430580
    void loadMissionVoices();                 // 0x430630
    void releaseMissionVoices();              // 0x430730
    void preloadPathVoices();                 // 0x430830
    void releasePathVoices();                 // 0x430990
    void updatePathCommentary();              // 0x430ad0
    void openMissionResource();               // 0x431050
    void closeMissionResource();              // 0x4310f0
    void setOutsidePalette();                 // 0x431180
    std::array<std::uint32_t, 256> loadPalette(std::string_view name); // 0x431200
    void copyUpperPaletteEntries(std::array<std::uint32_t, 256>& pal); // 0x431380
    void horseRestart();                      // 0x432c90
    void horseStop();                         // 0x432f30
    void updateMapLocation();                 // 0x433c10
    void showDecisionAnim(std::string_view name); // 0x433eb0
    void deleteDecisionAnim();                // 0x433fd0
    void clearStopProps();                    // 0x434000
    void sayStopComment();                    // 0x4344c0
    void showCertificateName();               // 0x434610
    void createHiddenAnim(std::string_view name);             // 0x434930
    engine::Anim* findOrCreateHiddenAnim(std::string_view name); // 0x434950
    void loadMountAnims();                    // 0x434990
    void loadWalkAnims();                     // 0x434ad0

    // --- TrailRide.cpp -----------------------------------------------------------------
    void initTrail();                         // 0x434cb0
    void updateWaterSounds();                 // 0x436c50
    int trailFrame() const;                   // 0x436e80 GetTrailFrame
    void setHorseFace(int face, bool force);  // 0x436f10
    void horseJump();                         // 0x437280
    void updateObstacle();                    // 0x4372f0
    void trailEnded();                        // 0x4377f0
    int startFrame() const;                   // 0x4378f0
    void switchTrail();                       // 0x437a40
    void loadTrail();                         // 0x437e50
    void loadBackupMovies();                  // 0x438930
    void unloadBackupMovies();                // 0x4390b0
    void playInsertMovie(std::string_view name); // 0x439160
    void loadGaitSounds();                    // 0x4394e0
    void releaseGaitSounds();                 // 0x4395f0
    void setGait(int gait);                   // 0x439660
    void playGaitSound();                     // 0x4396b0
    void stopGaitSound();                     // 0x439710
    void loadButterflies();                   // 0x439760
    void startButterfly();                    // 0x439a00
    void updateButterfly();                   // 0x439aa0
    void toggleMount();                       // 0x439ce0
    void setHorseState(int state);            // 0x43b240
    static void forceHideAnim(engine::Anim* anim); // 0x43baa0
    void forceHideAnimByName(std::string_view name); // 0x43bb10
    static void forceShowAnim(engine::Anim* anim); // 0x43bb30
    void walkBarbieToMouse();                 // 0x43bba0
    void mountAndRide();                      // 0x43be80
    void walkBarbieToHorse();                 // 0x43bf30

    int trail() const { return m_trail; }
    int decision() const { return m_decision; }
    bool riding() const { return m_riding; }

protected:
    // The trail-location subclasses (0x40a2f0…) set the map location (+0x7EC).
    void setMapLocation(int location) { m_mapLocation = location; }

    // --- mission, certificate and rescue code (0x43eca0–0x4457f0), not ported yet ---
    void c1LoadSounds();      // 0x43eca0
    void c1ReleaseSounds();   // 0x43ecd0
    void c1SetupStop();       // 0x43ed00
    void c1CleanupStop();     // 0x43ee10
    void c1RideTick();        // 0x43eeb0
    void c1MouseDown(const engine::Pick& pick); // 0x43efc0
    void c1CameraClick();     // 0x43f0b0
    void c2SetupStop();       // 0x43f2d0
    void c2CleanupStop();     // 0x43f460
    void c2ScriptEnded(std::string_view script); // 0x43f510
    void c2MouseDown(const engine::Pick& pick); // 0x43f5a0
    void c2CameraClick();     // 0x43f6b0
    void c2RideTick();        // 0x43f8e0
    void c2LoadSounds();      // 0x43f9d0
    void c2ReleaseSounds();   // 0x43fa50
    void c3SetupStop();       // 0x43fad0
    void c3CleanupStop();     // 0x43fb30
    void c3MouseDown(const engine::Pick& pick); // 0x43fb60
    void c3CameraClick();     // 0x43fc20
    void c3RideTick();        // 0x43fd50
    void c3LoadSounds();      // 0x43fdb0
    void c3ReleaseSounds();   // 0x43fdd0
    void c4LoadSounds();      // 0x43fdf0
    void c4ReleaseSounds();   // 0x43fe60
    void c4SetupStop();       // 0x43fec0
    void c4UpdateProgress();  // 0x43ff10
    void c4LoadTrail(int arg); // 0x440040
    void c4UnloadTrail();     // 0x440340
    void m1LoadSounds();      // 0x440580
    void m1ReleaseSounds();   // 0x440630
    void m1SetupStop();       // 0x4406d0
    void m1CleanupStop();     // 0x440870
    void m1RideTick();        // 0x4409e0
    void m1MouseDown(const engine::Pick& pick); // 0x440bb0
    void m1ScriptEnded(std::string_view script); // 0x441170
    void m2SetupStop();       // 0x4412e0
    void m2CleanupStop();     // 0x4414c0
    void m2MouseDown(const engine::Pick& pick); // 0x441550
    void m2RideTick();        // 0x441570
    void m2LoadSounds();      // 0x441670
    void m2ReleaseSounds();   // 0x4416c0
    void m3SetupStop();       // 0x441700
    void m3CleanupStop();     // 0x441a70
    void m3ScriptEnded(std::string_view script); // 0x441b60
    void m3RideTick();        // 0x441dd0
    void m3LoadSounds();      // 0x441e60
    void m3ReleaseSounds();   // 0x441e80
    void m4SetupStop();       // 0x441ea0
    void m4CleanupStop();     // 0x4421c0
    void m4RideTick();        // 0x442290
    void m4LoadSounds();      // 0x442320
    void m4ReleaseSounds();   // 0x442380
    void m4MouseDown(const engine::Pick& pick); // 0x4423d0
    void m4ScriptEnded(std::string_view script); // 0x4427b0
    void m5LoadSounds();      // 0x442810
    void m5ReleaseSounds();   // 0x4428c0
    void m5SetupStop();       // 0x442960
    void m5CleanupStop();     // 0x442af0
    void m5RideTick();        // 0x442c10
    void m5MouseDown(const engine::Pick& pick); // 0x442d30
    void m5ScriptEnded(std::string_view script); // 0x442de0
    void m6LoadSounds();      // 0x443000
    void m6ReleaseSounds();   // 0x4430a0
    void m6SetupStop();       // 0x443130
    void m6CleanupStop();     // 0x4433d0
    void m6RideTick();        // 0x443510
    void m6MouseDown(const engine::Pick& pick); // 0x443710
    void m6ScriptEnded(std::string_view script); // 0x443f50
    void m7SetupStop();       // 0x444080
    void m7CleanupStop();     // 0x4441e0
    void m7MouseDown(const engine::Pick& pick); // 0x444260
    void m7RideTick();        // 0x4442d0
    void m7LoadSounds();      // 0x444340
    void m7ReleaseSounds();   // 0x444360
    void m8SetupStop();       // 0x444380
    void m8CleanupStop();     // 0x444480
    void m8RideTick();        // 0x4444f0
    void m8MouseDown(const engine::Pick& pick); // 0x444570
    void m8LoadSounds();      // 0x444590
    void m8ReleaseSounds();   // 0x4445b0
    void r2SetupStop();       // 0x4445d0
    void r2CleanupStop();     // 0x444740
    void r2RideTick();        // 0x4447c0
    void r2LoadSounds();      // 0x444810
    void r2ReleaseSounds();   // 0x444830
    void r2MouseDown(const engine::Pick& pick); // 0x444850
    void r1SetupStop();       // 0x444920
    void r1CleanupStop();     // 0x444b30
    void r1RideTick();        // 0x444bd0
    void r1LoadSounds();      // 0x444c50
    void r1ReleaseSounds();   // 0x444c80
    void r1MouseDown(const engine::Pick& pick); // 0x444cb0
    void r3SetupStop();       // 0x444e80
    void r3CleanupStop();     // 0x444fe0
    void r3MouseDown(const engine::Pick& pick); // 0x445080
    void r3ScriptEnded(std::string_view script); // 0x445200
    void r3RideTick();        // 0x445290
    void r3LoadSounds();      // 0x445300
    void r3ReleaseSounds();   // 0x445320
    void lessonLoadSounds();  // 0x445340
    void lessonReleaseSounds(); // 0x4453b0
    void lessonRideTick();    // 0x445420
    void lessonIdleHint();    // 0x4457f0

private:
    int mission() const;      // app+0x310
    std::string smkPath(std::string_view name) const; // +0x1CC + name
    std::string headPath(std::string_view name) const; // +0x250 + name
    SmackOverlayMovie* backup(int trail, int index) const {
        return m_backups[static_cast<std::size_t>(trail * 3 + index)].get();
    }
    SmackOverlayMovie* currentBackup() const;
    void applyPan(SmackOverlayMovie* m) const;
    void deleteMovie(std::unique_ptr<SmackOverlayMovie>& m);
    void deleteAnim(engine::Anim*& anim);
    void runMissionRideTick();
    void stopScriptIfRunning(std::string_view name);

    // --- movies -------------------------------------------------------------------------
    std::unique_ptr<SmackOverlayMovie> m_base;          // +0x2D4: the trail movie
    std::array<std::unique_ptr<SmackOverlayMovie>, 18> m_backups; // +0x2D8 [trail*3 + i]: refusal loops
    std::unique_ptr<SmackOverlayMovie> m_insert;        // +0x320: event insert (Sparkle, HoofPrints, ...)
    SmackOverlayMovie* m_current = nullptr;             // +0x324: base, insert or a backup
    std::int16_t m_panX = 0x28;                         // +0x328: head/pan offset 0..0x50, centre 0x28
    std::int16_t m_panY = 0x28;                         // +0x32A: vertical offset (the jump arc)
    bool m_mountAnimsPending = true;                    // +0x32C
    bool m_riding = true;                               // +0x330
    bool m_runOnAfterRunOff = false;                    // +0x332
    bool m_mountAfterWalk = false;                      // +0x334
    bool m_rideOffAfterMount = false;                   // +0x336
    bool m_walkDisabled = false;                        // +0x338
    bool m_inObstacle = false;                          // +0x33A
    bool m_skip100 = false;                             // +0x33C: trail 0 entered at 0x38: frame 100 -> 172
    bool m_skip54 = false;                              // +0x33E: trail 0 entered at 0: frame 54 -> 166
    bool m_skip57 = false;                              // +0x340: trail 2 entered at 0: frame 57 -> 115
    bool m_enteredAtLoop = false;                       // +0x342: trail 4 entered at 0x447
    using Overlay = SmackOverlayMovie::Overlay;
    Overlay* m_headLeft = nullptr;                      // +0x344
    Overlay* m_headStraight = nullptr;                  // +0x348
    Overlay* m_headRight = nullptr;                     // +0x34C
    Overlay* m_headJump = nullptr;                      // +0x350
    Overlay* m_insertHeadLeft = nullptr;                // +0x354
    Overlay* m_insertHeadStraight = nullptr;            // +0x358
    Overlay* m_insertHeadRight = nullptr;               // +0x35C
    Overlay* m_insertHeadJump = nullptr;                // +0x360
    std::array<Overlay*, 18> m_reverseHeads{};          // +0x364 [trail*3 + i]
    int m_musicId = 0;                                  // +0x3AC
    int m_ambientIndex = 5;                             // +0x3B0
    int m_gait = 0;                                     // +0x3B4
    bool m_gaitPlaying = false;                         // +0x3B8
    engine::Sound* m_falls = nullptr;                   // +0x3BC
    engine::Sound* m_stream = nullptr;                  // +0x3C0
    bool m_pauseCopy = false;                           // +0x3C4: [0x80a438]
    std::deque<std::string> m_preloadQueue;             // +0x3C8 / +0x3CC
    bool m_deerDone = false;                            // +0x3D0
    bool m_deerVisible = false;                         // +0x3D2
    Overlay* m_deer = nullptr;                          // +0x3D4
    engine::Anim* m_decisionAnim = nullptr;             // +0x3D8
    bool m_faceCheck = false;                           // +0x3DC: only ever cleared (the check is dormant)
    int m_face = 1;                                     // +0x3E0: 0 left, 1 straight, 2 right
    int m_faceCountdown = 0;                            // +0x3E4
    int m_obstacle = 0;                                 // +0x3E8
    bool m_jumping = false;                             // +0x3EC
    int m_jumpPhase = 0;                                // +0x3F0
    bool m_keySlower = false;                           // +0x3F4 (key 9)
    bool m_keyFaster = false;                           // +0x3F6 (key 8)
    bool m_keyLeft = false;                             // +0x3F8 (key 6)
    bool m_keyRight = false;                            // +0x3FA (key 7)
    std::int16_t m_walkTargetX = 0, m_walkTargetY = 0;  // +0x3FC / +0x3FE
    int m_trail = 0;                                    // +0x400
    int m_nextTrail = 0;                                // +0x404
    int m_startFrame = 0;                               // +0x408
    bool m_restartSameTrail = false;                    // +0x40C
    std::array<std::unique_ptr<std::array<std::uint32_t, 256>>, kTrails> m_trailPalettes; // +0x410
    std::array<std::uint32_t, 256> m_outsidePalette{};  // +0x430
    std::array<std::uint32_t, 256> m_horsePalette{};    // +0x434
    int m_stopMission = 0x11;                           // +0x438
    bool m_startFramePreset = false;                    // +0x43C
    int m_horseState = kNoState;                        // +0x440
    bool m_highMemory = true;                           // +0x444
    bool m_ambientPreemptToggle = true;                 // +0x446
    engine::Anim* m_bMountRun = nullptr;                // +0x448
    engine::Anim* m_hMountRun = nullptr;                // +0x44C
    engine::Anim* m_walk = nullptr;                     // +0x450 barbieWalkOutside
    engine::Anim* m_saddleAndReigns = nullptr;          // +0x454
    engine::Anim* m_generalAmbHorse = nullptr;          // +0x458
    engine::Anim* m_subtleAmbHorse = nullptr;           // +0x45C
    engine::Anim* m_generalAmbSdl = nullptr;            // +0x460
    engine::Anim* m_subtleAmbSdl = nullptr;             // +0x464
    engine::Anim* m_ambientBarbie = nullptr;            // +0x468
    engine::Anim* m_ambientHorse = nullptr;             // +0x46C
    engine::Anim* m_barbieAmbients = nullptr;           // +0x470
    bool m_field474 = true;                             // +0x474 (set, never read here)
    bool m_tenderMomentAfterAmb = false;                // +0x476
    engine::Anim* m_bTMoment = nullptr;                 // +0x478
    engine::Anim* m_hTMoment = nullptr;                 // +0x47C
    engine::Anim* m_sTMoment = nullptr;                 // +0x480
    bool m_fancy = false;                               // +0x484
    engine::Anim* m_fancyMountRun = nullptr;            // +0x488
    engine::Anim* m_generalAmbFancy = nullptr;          // +0x48C
    engine::Anim* m_subtleAmbFancy = nullptr;           // +0x490
    engine::Anim* m_ambientFancy = nullptr;             // +0x494
    engine::Anim* m_fTMoment = nullptr;                 // +0x498
    bool m_butterflies = false;                         // +0x49C
    // +0x4A4: Barbie on the horse at a stop. Only HorseStop and the scripts set
    // it; the constructor leaves it as allocated (zero here, inferred).
    bool m_mounted = false;
    int m_walkCycles = 0;                               // +0x4A8
    int m_walkDirection = 0;                            // +0x4AC
    int m_lastDirection = 0;                            // +0x4B0 (2 = just dismounted)
    int m_barbieSlot = 0;                               // +0x4B4: 0..4, 1 at the saddle, 2 at the head
    std::string m_ambientLoop;                          // +0x4B8
    std::array<std::array<std::string, 5>, kTrails> m_gaitSounds; // +0x4BC [trail][gait]
    bool m_skipStopComment = false;                     // +0x64C
    std::array<int, 7> m_commentCount{};                // +0x650 [map location]
    bool m_rideHintPlayed = false;                      // +0x66C
    std::array<Overlay*, 3> m_butterfly{};              // +0x670: _20, _50, _80
    bool m_butterflyActive = false;                     // +0x67C
    int m_butterflyCount = 0;                           // +0x680
    int m_butterflyStage = 0;                           // +0x684
    int m_butterflyDx = 1;                              // +0x688 (+1 = REV, flying right)
    int m_butterflyDy = 1;                              // +0x68C
    engine::Anim* m_signA = nullptr;                    // +0x690: aat/rrt rr sign
    engine::Anim* m_signB = nullptr;                    // +0x694: rrt ob sign
    std::unique_ptr<ACRollover> m_signARollover;        // +0x698
    std::unique_ptr<ACRollover> m_signBRollover;        // +0x69C
    engine::Anim* m_critter = nullptr;                  // +0x6A0
    std::unique_ptr<ACRollover> m_critterRollover;      // +0x6A4
    engine::Anim* m_owl = nullptr;                      // +0x6A8
    std::unique_ptr<ACRollover> m_owlRollover;          // +0x6AC
    engine::Anim* m_cow = nullptr;                      // +0x6B0
    std::unique_ptr<ACRollover> m_cowRollover;          // +0x6B4
    int m_decision = 5;                                 // +0x6B8: decision at the stop, 5 = none
    engine::Anim* m_pickup = nullptr;                   // +0x6BC: item to pick up (click 0x2714)
    int m_pickupItem = 0;                               // +0x6C0: its bag item
    int m_missionResource = 0x11;                       // +0x6C4
    bool m_tutorialFinished = false;                    // +0x6C8
    int m_exitPropNode = 0;                             // +0x6CC: node-name index for click 0x2711
    engine::Anim* m_certificate = nullptr;              // +0x6D8
    std::shared_ptr<engine::Font> m_certificateFont;    // +0x6DC
    std::unique_ptr<engine::TextElement> m_certificateText; // +0x6E0
    engine::Anim* m_mailbox = nullptr;                  // +0x6E4
    std::unique_ptr<ACRollover> m_mailboxRollover;      // +0x6F0
    std::array<bool, 3> m_hoofPrintsShown{};            // +0x728 / +0x72A / +0x72C: trails 4, 0, 5
    engine::Anim* m_gate = nullptr;                     // +0x730: m4 gate
    std::unique_ptr<ACRollover> m_gateRollover;         // +0x740
    std::array<bool, 4> m_field74C{};                   // +0x74C..+0x752: set by the M5 arrival (mission code)
    engine::Anim* m_bush = nullptr;                     // +0x754: m4 / m6 bush
    engine::Anim* m_well = nullptr;                     // +0x758: wishing well
    std::unique_ptr<ACRollover> m_bushRollover;         // +0x764
    std::unique_ptr<ACRollover> m_wellRollover;         // +0x768
    bool m_db03066Played = false;                       // +0x774
    engine::Anim* m_bellAnim = nullptr;                 // +0x784 (mission 7)
    std::unique_ptr<ACRollover> m_bellRollover;         // +0x788
    // +0x780: a modal popup of the mission code (blocks "continue"); deleted
    // by the ride loop once its done flag (+0xF8) is set.
    struct ModalPopup {
        virtual ~ModalPopup() = default;
        virtual bool finished() const = 0;
    };
    std::unique_ptr<ModalPopup> m_modal;
    bool m_dropInsertOnRestart = false;                 // +0x7BE
    int m_mapLocation = 0;                              // +0x7EC
    int m_pagerDelay = 0;                               // +0x7F4
    bool m_pagerBusy = false;                           // +0x7F8
    std::string m_music;                                // +0x174 (toolbar): the music playing

    bool m_leftReleased = true;         // +0x1C0
    bool m_rightReleased = true;        // +0x1C4
    std::uint32_t m_lastInput = 0;      // +0x1C8
};

} // namespace brc::game
