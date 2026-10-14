# Game classes, node framework and object layouts

This document maps the C++ classes of the *Barbie Riding Club* game layer (Human Code "Godiva",
`E:\Projects\Omt\Godiva\*.cpp`, code range 0x401000–0x445820) plus the engine node base `HCNode`
(`HCNode.cpp`, 0x454320–0x458640): class hierarchy, constructors, object sizes, vtables, field layouts and the
meaning of every virtual slot. Per-node behaviour (screens, assets, transitions) is in [nodes.md](nodes.md); the
player record and other user files are in [../formats/savegame.md](../formats/savegame.md).

Related engine documents: [application.md](application.md) (main loop, node switching, fades),
[animation.md](animation.md) (HC2DAnim, click ids, anim callbacks), [scripts.md](scripts.md) (script VM and the
script-event broadcaster), [input.md](input.md) (mouse/keyboard events, edge cursors), [sound.md](sound.md),
[movies.md](movies.md).

Conventions:
- Addresses are virtual addresses in `Barbie Riding Club.exe` (image base 0x400000).
- `this+X` / `node+X` are byte offsets in the object; `app` is the `Godiva` application object (0x350 bytes,
  global pointer `[0x80a384]`, reachable from a toolbar node as node+0x1B8 and from a plain HCNode as node+0x90).
- "rec+X" is an offset in the saved player record, which lives at app+0x1B0 (rec+X = app+0x1B0+X).
- Virtual slots are written as byte offsets into the vtable (`vt+0x54` = slot 21).
- `local/re/classes.json` maps every function address listed here to `Class::Method` (1036 functions). Class names
  come from debug strings where they exist (`HCNode`, `HCToolbarNode`, `TrailNode`, `AppleSort`, `BarbieCamera`,
  `BarbiePager`, `Race`, `HCLogoNode`, `BarbieSoftLogoNode`, `MattelLogoNode`, `Godiva`, `ACRollover`/`Rollover`
  from the leak warnings); all other names are descriptive and marked *(name inferred)* in the catalogue.
- Items not proven by code are marked *(inferred)*.
- Helper shorthands: `SetHidden(b)` = anim vt+0x30(b) (a hide counter, anim+0x48); `SetSeq(a,n)` = `(a+0xC4)->vt+4(n,1)` (play sequence n; anim+0xD8 = current sequence); `ForceHideAnim`/`ForceShowAnim` (`0x43baa0`/`0x43bb30`, copies at `0x401520`/`0x401580`) call vt+0x30/vt+0x38 until the anim is hidden and paused / visible and running; `HasBagItem` = `0x41d200`; `PlayVoicePaused` = `0x41d310` ("pause" then the voice); the "cursor-hotspot helper" is an `ACRollover` (`0x401160`, hand cursor while the mouse is over one anim).

## Source files and address ranges

The linker emitted one object file per source file, in alphabetical order. File names marked † are inferred
from that order and the class names; the others are proven by `__FILE__` strings.

| range | source file | classes / contents |
|---|---|---|
| 0x401000–0x4012ff | ACRollover.cpp† | ACButton, ACRollover |
| 0x401400–0x403f1f | AppleSort.cpp† | AppleSort (+ COMDAT copies of SaveGame, String helpers, empty HCNode hooks) |
| 0x403f20–0x404f6f | BarbieCamera.cpp† | BarbieCamera (photo store) |
| 0x404f70–0x405a6f | BarbieMap.cpp† | BarbieMap, MapVoiceRollover, BarbiePopup |
| 0x405a70–0x406baf | BarbiePager.cpp† | BarbiePager |
| 0x406bb0–0x409c5f | BarbieSaddlebag.cpp† | BarbieSaddlebag |
| 0x409c60–0x40a07f | BarbieSaveGame.cpp† | save-path statics, CreateNewSaveGame |
| 0x40a080–0x40a2ef | BarbieSoftLogoNode.cpp† | BarbieSoftLogoNode |
| 0x40a2f0–0x40a97f | (trail locations)† | AppleWest, BeachEast, CottageWayLoop, OldBridgePath, RabbitEast, RainbowFalls, WishingWayLoop |
| 0x40a980–0x40bb8f | bellgame.cpp† | BellGame |
| 0x40bb90–0x40ec3f | bulletinboard.cpp† | BBPanel family, BBTextStyle, BBNameField, BulletinBoard |
| 0x40ec40–0x40f85f | (button)† | HCRolloverButton + HP-STL list/vector helpers |
| 0x40f860–0x410d1f | carousel.cpp† | Carousel |
| 0x410d20–0x410fdf | credits.cpp† | Credits |
| 0x410fe0–0x41180f | DesktopHorse.cpp | DesktopHorse free functions, ComInitializer |
| 0x411810–0x411bcf | finalpayoff.cpp† | FinalPayoff, FinalPayoffBackground |
| 0x411bd0–0x41393f | Godiva.cpp | Godiva application, CreateNewNode, node music |
| 0x413940–0x41524f | grooming.cpp† | Grooming |
| 0x415250–0x41934f | (handbook)† | Handbook, HandbookPrintDialog, HandbookPhotoAlbum, HandbookAwardsPage |
| 0x419350–0x4194ef | (log)† | Log::Write, log rotation |
| 0x4194f0–0x41a2ef | HCLogoNode.cpp | HCLogoNode |
| 0x41a300–0x41b68f | (Smacker elements)† | HCSmackerElement, HCSmackerOverlayList, HCSmackerOverlayElement |
| 0x41b690–0x41e98f | HCToolbarNode.cpp† | HCToolbarNode |
| 0x41e990–0x41f1ef | IntroMovie.cpp | IntroMovie |
| 0x41f1f0–0x420f5f | leapfrog.cpp† | Leapfrog, LeapfrogPad |
| 0x420f60–0x4211ff | logos.cpp† | MattelLogoNode (+ shared logo helpers) |
| 0x421200–0x4216cf | opendoor.cpp† | OpenDoor |
| 0x4216d0–0x4225ff | preferences.cpp† | Preferences, PrefsCheckBox |
| 0x422600–0x422cff | (print)† | Print helpers |
| 0x422d00–0x4269df | race.cpp† | Race |
| 0x4269e0–0x426edf | (rollover, shortcut)† | Rollover, DesktopShortcut |
| 0x426ee0–0x429d8f | signin.cpp† | SignInCard, SignIn, SignIn text styles, LoadGame / SlotExists / DeleteSavegame |
| 0x429d90–0x42c0af | stable.cpp† | Stable |
| 0x42c0b0–0x42fb0f | tackroom.cpp† | TackRoom |
| 0x42fb10–0x42fd9f | (timer)† | HCTimer |
| 0x42fda0–0x434caf | TrailNode.cpp | TrailNode (critters, mission voices, palettes, ctor/dtor, stops) |
| 0x434cb0–0x43c0bf | TrailRide.cpp | TrailNode (ride loop, trails, horse states) |
| 0x43c0c0–0x43cd9f | vinegame.cpp† | VineGame |
| 0x43cda0–0x43ec9f | yard.cpp | Yard |
| 0x43eca0–0x44581f | (missions)† | TrailNode mission / certificate / rescue methods |
| 0x445820– | exec.cpp … | HC engine (script executor, HC2DAnim, HCApplication, HCMovie, HCNode, …) |

## Class hierarchy

```
OMediaWorld (OMT 1.x; +0 world, +0xC OMediaPeriodical, +0x18 OMediaListener, +0x6C render iface)
 └─ HCNode  (+0x7C OMediaBroadcaster)                        0x454320, 0xC4 bytes
     ├─ MattelLogoNode / HCLogoNode / BarbieSoftLogoNode        timed splash nodes
     └─ HCToolbarNode                                         0x41b690, 0x1C0 bytes
         ├─ Preferences, SignIn, IntroMovie, Yard, Stable, TackRoom, Grooming, BulletinBoard
         ├─ AppleSort, Leapfrog, BellGame, VineGame, Carousel, OpenDoor, Race, FinalPayoff, Credits
         └─ TrailNode                                         0x431420, 0x7FC bytes
             └─ AppleWest, CottageWayLoop, OldBridgePath, RainbowFalls, RabbitEast, BeachEast, WishingWayLoop

HCApplication (engine, 0x44a0e0) └─ Godiva                    0x411bd0, 0x350 bytes

OMediaPeriodical
 ├─ Rollover (mouse-hover poller)                             0x4269e0
 │   (embedded at +0x10 of) HCRolloverButton (+ OMediaListener at +0)   0x40ec40, 0xC8 bytes
 │        ├─ ACButton (adds an ACRollover)                    0x401000, 0xD4 bytes
 │        └─ LeapfrogPad (adds a frog pointer)                0xCC bytes
 ├─ ACRollover (hand cursor over one anim)                    0x401160, 0xA0 bytes
 │   └─ MapVoiceRollover (+ one-shot voice)                   0xA8 bytes
 ├─ HCTimer (+ OMediaBroadcaster at +0xC)                     0x42fb10, 0x34 bytes
 └─ (also a base of BarbiePager at +0, BarbieSaddlebag at +4, HCSmackerElement at +0xD0)

BarbiePopup (toolbar popup interface, vtable 0x7f0218)
 ├─ BarbieMap (app+0x188)        ├─ BarbiePager (node+0xD8, at +0xC)   └─ BarbieSaddlebag (node+0xD4)

BBPanel (popup page over the bulletin board, vtable 0x7f0c18)
 ├─ BBClueBook, BBTeamPhoto, BBMembershipCards, BBCertificates
 └─ Handbook, HandbookPrintDialog          (HandbookPhotoAlbum and HandbookAwardsPage are separate small classes)

OMT canvas element  ├─ HCSmackerElement      └─ (HCSmackerOverlayList +) HCSmackerOverlayElement
OMT string field    └─ BBNameField (text element used for player names)
text style base (0x7f0d98) ├─ BBTextStyle  ├─ SignInNameTextStyle  └─ SignInCardTextStyle
ComInitializer └─ DesktopShortcut
Free functions: SaveGame/CreateNewSaveGame, DesktopHorse::*, Print::*, Log::*, String helpers
```

## Class catalogue

Sizes are the `operator new` sizes used by the factory or the creating code. "sdd" = scalar deleting destructor.
Vtables are listed as main / +0xC / +0x18 / +0x6C / +0x7C for nodes.

| class | kind | ctor | dtor / sdd | size | vtables | notes |
|---|---|---|---|---|---|---|
| HCNode | node base (engine) | 0x454320 | 0x454880 / 0x454860 | 0xC4 | 0x7f45f8 / 0x7f45f0 / 0x7f45e0 / 0x7f45d8 / 0x7f45c8 | 50 main slots |
| HCToolbarNode | node base | 0x41b690 | 0x41bc30 / 0x41bc10 | 0x1C0 | 0x7f1b18 / 0x7f1b10 / 0x7f1b00 / 0x7f1af8 / 0x7f1ae8 | 55 main slots; called "Godiva node base" in other docs |
| MattelLogoNode | node "MattelLogoNode" | 0x420f60 | 0x4210a0 / 0x421080 | 0xD0 | 0x7f1e88 / 0x7f1e80 / 0x7f1e70 / 0x7f1e68 / 0x7f1e58 | HCNode-derived |
| HCLogoNode | node "HCLogoNode" | 0x4194f0 | 0x419ab0 / 0x419a90 | 0xD8 | 0x7f1828 / 0x7f1820 / 0x7f1810 / 0x7f1808 / 0x7f17f8 | HCNode-derived |
| BarbieSoftLogoNode | node "BarbieSoftLogoNode" | 0x40a080 | 0x40a1c0 / 0x40a1a0 | 0xD0 | 0x7f02a8 / 0x7f02a0 / 0x7f0290 / 0x7f0288 / 0x7f0278 | HCNode-derived |
| Preferences *(name inferred)* | node "preferences" | 0x4216d0 | 0x421e20 / 0x421e00 | 0x1FC | 0x7f3110 / 0x7f3108 / 0x7f30f8 / 0x7f30f0 / 0x7f30e0 | |
| SignIn *(name inferred)* | node "signin" | 0x427370 | 0x4287b0 / 0x4282e0 | 0x25C | 0x7f3420 / 0x7f3418 / 0x7f3408 / 0x7f3400 / 0x7f33f0 | |
| IntroMovie | node "intromovie" | 0x41e990 | 0x41ebb0 / 0x41eb90 | 0x1C8 | 0x7f1c28 / 0x7f1c20 / 0x7f1c10 / 0x7f1c08 / 0x7f1bf8 | |
| Yard | node "yard" (+ factory fallback) | 0x43cda0 | 0x43d660 / 0x43d640 | 0x214 | 0x7f3fb0 / 0x7f3fa8 / 0x7f3f98 / 0x7f3f90 / 0x7f3f80 | |
| Stable *(name inferred)* | node "stable" | 0x429d90 | 0x42a460 / 0x42a440 | 0x25C | 0x7f3530 / 0x7f3528 / 0x7f3518 / 0x7f3510 / 0x7f3500 | |
| TackRoom *(name inferred)* | node "tackroom" | 0x42c0b0 | 0x42c3f0 / 0x42c3d0 | 0x2A0 | 0x7f3640 / 0x7f3638 / 0x7f3628 / 0x7f3620 / 0x7f3610 | |
| Grooming *(name inferred)* | node "grooming" | 0x413940 | 0x413b20 / 0x413b00 | 0x258 | 0x7f1678 / 0x7f1670 / 0x7f1660 / 0x7f1658 / 0x7f1648 | |
| BulletinBoard *(name inferred)* | node "bulletinboard" | 0x40def0 | 0x40e970 / 0x40e270 | 0x1F8 | 0x7f0df8 / 0x7f0df0 / 0x7f0de0 / 0x7f0dd8 / 0x7f0dc8 | adds virtual +0xDC OpenPanel |
| TrailNode | trail node base | 0x431420 | 0x431fa0 / 0x431f60 | 0x7FC | 0x7f39f8 / 0x7f39f0 / 0x7f39e0 / 0x7f39d8 / 0x7f39c8 | ctor(app, name, trailId) |
| AppleWest *(name inferred)* | node "appleW" | 0x40a2f0 | 0x40a370 / 0x40a350 | 0x7FC | 0x7f03a0 / 0x7f0398 / 0x7f0388 / 0x7f0380 / 0x7f0370 | trail 0 |
| CottageWayLoop *(name inferred)* | node "cottageL" | 0x40a450 | 0x40a4d0 / 0x40a4b0 | 0x7FC | 0x7f05c0 / 0x7f05b8 / 0x7f05a8 / 0x7f05a0 / 0x7f0590 | trail 1 |
| OldBridgePath *(name inferred)* | node "bridgeS" | 0x40a500 | 0x40a580 / 0x40a560 | 0x7FC | 0x7f06d0 / 0x7f06c8 / 0x7f06b8 / 0x7f06b0 / 0x7f06a0 | trail 2 |
| RainbowFalls *(name inferred)* | node "rainbowS" | 0x40a660 | 0x40a6e0 / 0x40a6c0 | 0x7FC | 0x7f08f0 / 0x7f08e8 / 0x7f08d8 / 0x7f08d0 / 0x7f08c0 | trail 3 |
| RabbitEast *(name inferred)* | node "rabbitE" | 0x40a5b0 | 0x40a630 / 0x40a610 | 0x7FC | 0x7f07e0 / 0x7f07d8 / 0x7f07c8 / 0x7f07c0 / 0x7f07b0 | trail 4 |
| BeachEast *(name inferred)* | node "beachE" | 0x40a3a0 | 0x40a420 / 0x40a400 | 0x7FC | 0x7f04b0 / 0x7f04a8 / 0x7f0498 / 0x7f0490 / 0x7f0480 | trail 5 |
| WishingWayLoop *(name inferred)* | node "wishingL" | 0x40a710 | 0x40a790 / 0x40a770 | 0x7FC | 0x7f0a00 / 0x7f09f8 / 0x7f09e8 / 0x7f09e0 / 0x7f09d0 | trail 6 |
| AppleSort | node "applesort" | 0x4015e0 | 0x401bd0 / 0x401bb0 | 0x270 | 0x7f0118 / 0x7f0110 / 0x7f0100 / 0x7f00f8 / 0x7f00e8 | mini-game (M5) |
| Leapfrog *(name inferred)* | node "leapfrog" | 0x41f1f0 | 0x41f9e0 / 0x41f900 | 0x270 | 0x7f1d78 / 0x7f1d70 / 0x7f1d60 / 0x7f1d58 / 0x7f1d48 | mini-game (M2) |
| BellGame *(name inferred)* | node "bellgame" | 0x40a980 | 0x40ab90 / 0x40ab70 | 0x25C | 0x7f0b10 / 0x7f0b08 / 0x7f0af8 / 0x7f0af0 / 0x7f0ae0 | mini-game (M7) |
| VineGame *(name inferred)* | node "vinegame" | 0x43c0c0 | 0x43c770 / 0x43c750 | 0x28C | 0x7f3e98 / 0x7f3e90 / 0x7f3e80 / 0x7f3e78 / 0x7f3e68 | mini-game (M8) |
| Carousel *(name inferred)* | node "carousel" | 0x40f860 | 0x40fa70 / 0x40fa50 | 0x224 | 0x7f0f28 / 0x7f0f20 / 0x7f0f10 / 0x7f0f08 / 0x7f0ef8 | cottage interior (M4) |
| OpenDoor *(name inferred)* | node "opendoor" | 0x421200 | 0x421440 / 0x421420 | 0x1C4 | 0x7f3000 / 0x7f2ff8 / 0x7f2fe8 / 0x7f2fe0 / 0x7f2fd0 | |
| Race | node "race" | 0x422d00 | 0x423cc0 / 0x423ca0 | 0x284 | 0x7f32c0 / 0x7f32b8 / 0x7f32a8 / 0x7f32a0 / 0x7f3290 | |
| FinalPayoff *(name inferred)* | node "finalpayoff" | 0x411810 | 0x411980 / 0x411960 | 0x1C4 | 0x7f11e8 / 0x7f11e0 / 0x7f11d0 / 0x7f11c8 / 0x7f11b8 | |
| Credits *(name inferred)* | node "credits" | 0x410d20 | 0x410e40 / 0x410e20 | 0x1C8 | 0x7f1060 / 0x7f1058 / 0x7f1048 / 0x7f1040 / 0x7f1030 | |
| Godiva | application | 0x411bd0 | 0x412080 / 0x411c50 | 0x350 | 0x7f1600 (+0x20 0x7f15f0, +0x30 0x7f15e8) | derives HCApplication |
| Rollover | hover poller | 0x4269e0 | 0x426a60 / 0x426a40 | ≈0x9C (embedded) | 0x7f33a0 | live count `0x80a13c` |
| HCRolloverButton *(name inferred)* | 3-state button | 0x40ec40 | 0x40ed00 / 0x40ece0 | 0xC8 | 0x7f0ee8 (+0x10 0x7f0ed8) | toolbar buttons |
| ACButton *(name inferred)* | button + hand cursor | 0x401000 | 0x4010e0 / 0x401040 | 0xD4 | 0x7f0010 (+0x10 0x7f0000) | |
| ACRollover | hand-cursor rollover | 0x401160 | 0x4011f0 / 0x4011d0 | 0xA0 | 0x7f0020 | live count `0x809930` |
| MapVoiceRollover *(name inferred)* | ACRollover + voice | inline | 0x405380 / 0x405360 | 0xA8 | 0x7f0230 | |
| LeapfrogPad *(name inferred)* | lily pad button | inline | 0x420f30 / 0x420f10 | 0xCC | 0x7f1d38 (+0x10 0x7f1d28) | |
| BarbiePopup *(name inferred)* | popup interface | inline | 0x405b30 / 0x404fe0 | — | 0x7f0218 | |
| BarbieMap *(name inferred)* | toolbar map popup | 0x404f70 | 0x404fb0 / 0x404f90 | 0x58 | 0x7f01f8 | app+0x188 |
| BarbiePager | toolbar pager popup | 0x405a70 | 0x406110 / 0x405b40 | 0x70 | 0x7f0250 (+0xC 0x7f0238) | node+0xD8 |
| BarbieSaddlebag *(name inferred)* | toolbar inventory popup | 0x406bb0 | 0x4070a0 / 0x406c40 | 0x30 | 0x7f0260 (+4 0x7f0258) | node+0xD4 |
| BarbieCamera | photo store | 0x404080 | — | 1 (no members) | — | app+0x184 |
| BBPanel *(name inferred)* | bulletin popup base | inline | 0x40bd70 / 0x40bdf0 | 0xC | 0x7f0c18 | |
| BBClueBook *(name inferred)* | clue book | 0x40bb90 | 0x40be50 / 0x40be30 | 0x28 | 0x7f0bf8 | |
| BBTeamPhoto *(name inferred)* | team photo | 0x40c7f0 | 0x40c890 / 0x40c870 | 0x10 | 0x7f0c38 | |
| BBMembershipCards *(name inferred)* | membership card | 0x40c960 | 0x40cf40 / 0x40cf20 | 0x20 | 0x7f0d78 | |
| BBCertificates *(name inferred)* | certificates book | 0x40d090 | 0x40d3e0 / 0x40d3c0 | 0x58 | 0x7f0da8 | |
| Handbook *(name inferred)* | handbook popup | 0x417d40 | 0x4180f0 / 0x4180d0 | 0x6C | 0x7f17c0 | |
| HandbookPrintDialog *(name inferred)* | print dialog | 0x415250 | 0x415670 / 0x415470 | 0x40 | 0x7f1780 | |
| HandbookPhotoAlbum *(name inferred)* | photo album | 0x416610 | 0x416a60 / — | 0x128 | 0x7f17a0 | non-virtual dtor |
| HandbookAwardsPage *(name inferred)* | awards page | 0x417490 | 0x417920 / 0x417900 | 0x3C | 0x7f17b0 | |
| BBTextStyleBase / BBTextStyle *(names inferred)* | font holder | 0x40cce0 / inline | 0x40cd20, 0x40cd90 / 0x40cd50, 0x40cd70 | 0x14 | 0x7f0d98 / 0x7f0d68 | |
| SignInNameTextStyle / SignInCardTextStyle *(names inferred)* | font holders | inline | 0x427fc0 / 0x427fa0, 0x428030 / 0x428010 | 0x14 | 0x7f33e0 / 0x7f33d0 | |
| BBNameField *(name inferred)* | OMT text element | inline (base 0x465d10) | 0x40cef0 (sdd) | 0x168 | 0x7f0ca8 + 6 secondary | player-name text |
| SignInCard *(name inferred)* | sign-in card | 0x426ee0 | 0x427130 / 0x427110 | 0x1C | 0x7f33b8 | |
| PrefsCheckBox *(name inferred)* | check box | inline | — / 0x421dc0 | 0x10 | 0x7f30dc | |
| HCTimer *(name inferred)* | periodic message timer | 0x42fb10 | 0x42fba0 / 0x42fb80 | 0x34 | 0x7f3738 (+0xC 0x7f3760) | idle timer etc. |
| HCSmackerElement *(name inferred)* | SMK canvas element | 0x41a300 | 0x41a500 / 0x41a2e0 | 0x114 | 0x7f1918 (+0x10, +0x1C, +0x6C, +0xD0) | |
| HCSmackerOverlayList *(name inferred)* | overlay list mixin | 0x41aa30 | 0x41aa80 | — | 0x7f19c0 | |
| HCSmackerOverlayElement *(name inferred)* | SMK + overlays | 0x41ab50 | 0x41ac60 / 0x41ac30 | 0x130 | 0x7f1aa0, 0x7f19f8, … | trail movies |
| FinalPayoffBackground *(name inferred)* | solid backdrop | inline | 0x411950 / 0x411930 | 0x14 | 0x7f11a0 | |
| ComInitializer *(name inferred)* | COM refcount base | inline | 0x411230 / 0x4111f0 | — | 0x7f119c | |
| DesktopShortcut *(name inferred)* | IShellLink wrapper | 0x426c90 | — / 0x426cc0 | ≥0x410 (stack) | 0x7f33b0 | |

## Node framework

### Inheritance and the five vtable pointers

Every node (screen) is an `HCNode` (`E:\Projects\Omt\Godiva\HCNode.cpp`, ctor `0x454320`), and every node
with the in-game toolbar is an `HCToolbarNode` (ctor `0x41b690`, game side, between HCLogoNode.cpp and
IntroMovie.cpp). `HCNode` itself derives from the OMT 1.x world class (`OMediaWorld`, `Animation\Abstract_World\
OMediaWorld.cpp`; ctor `0x45c710`, built on `0x45c900`) plus an `OMediaBroadcaster` sub-object at +0x7C (ctor
`0x45c2b0`). The world part brings the other sub-objects, so a node object carries five vtable pointers:

| this+ | sub-object (base) | vtable slots (HCNode table / HCToolbarNode table) |
|---|---|---|
| +0x00 | main (OMediaWorld → HCNode → HCToolbarNode → node) | 50 slots for HCNode (`0x7f45f8`), 55 for HCToolbarNode (`0x7f1b18`), see below |
| +0x0C | `OMediaPeriodical` (idle task, called every main-loop pass) | `[0]` dtor thunk (`0x458350`/`0x41e950`), `[1]` **SpendTime** (`0x456f30` / `0x41cba0`) — `0x7f45f0` / `0x7f1b10` |
| +0x18 | `OMediaListener` (receives broadcaster messages; mouse relay, sounds) | `[0]` **ListenToMessage(msg, param)** (`0x454b50` / `0x41c910`), `[1]` dtor thunk, `[2]` `0x45bd80` (detach from all broadcasters) — `0x7f45e0` / `0x7f1b00` |
| +0x6C | world render interface | `[0]` dtor thunk, `[1]` `0x403f00` = thunk to main slot 8 (`0x45c880`) — `0x7f45d8` / `0x7f1af8` |
| +0x7C | `OMediaBroadcaster` (node broadcasts to its listeners) | `[0]` dtor thunk, `[1]` Broadcast(msg,param) `0x45c580`, `[2]` AddListener `0x45c510`, `[3]` RemoveListener `0x45c470` — `0x7f45c8` / `0x7f1ae8` |

Derived nodes repeat the same five-pointer layout with their own tables (the factory nodes' tables are listed
in the class catalogue). Only the main table and the +0x0C/+0x18 tables ever get node-specific overrides.

*(inferred names)* The OMT base names come from OMT 2.5 analogues (`OMediaPeriodical::spend_time`,
`OMediaListener::listen_to_message`, `OMediaBroadcaster`); the 1.x slot order differs from 2.5.

### HCNode object layout (0xC4 bytes)

| off | type | meaning |
|---|---|---|
| 0x00 | vtbl* | main vtable |
| 0x04 | object | OMT member constructed by `0x466fa0` (world element container) |
| 0x0C | vtbl* | periodical sub-object |
| 0x18 | vtbl* | listener sub-object (+0x1C: list of broadcasters it listens to) |
| 0x28 | list* | world element/layer list (iterated by Render `0x45cff0`) |
| 0x30 | int | time mode (set by slot +0x14) |
| 0x34 | int | frame interval ms (slot +0x18; `0x45cee0` keeps a 16-frame running average, default 20) |
| 0x40 | u32 | max elapsed per tick (500 ms) |
| 0x48 | int | pause nesting count (slot +0x2C) |
| 0x4C | u16 | invalidate flag (slot +0x28) |
| 0x50/0x54/0x58 | u32 | timer: start tick, elapsed, accumulated (GetTickCount based, pause aware) |
| 0x5C/0x5E | u16 | timer paused / frozen flags |
| 0x68 | obj* | current canvas/background object (slot +0x24 SetCanvas) |
| 0x6C | vtbl* | render sub-object |
| 0x74 | u16 | =1 (ctor) |
| 0x78 | int | =3 (ctor) |
| 0x7C | vtbl* | broadcaster sub-object; +0x80 listener list, +0x84 count |
| 0x88 | char* | node name (copied from the factory name), +0x8C u16 length |
| 0x90 | HCApplication* | the app (first ctor argument); ctor calls `app->vtbl[0x24](this)` |
| 0x94 | layer* | the node's display layer/viewport (`new(0xC4)` `0x492ed0(this)`): 640×480 rectangle centred on the screen, left edge rounded down to a multiple of 64; +0x40/+0x44 float origin used by `CreateAnimAtViewport`, +0xAC..+0xB8 int rect (left, top, right, bottom) |
| 0x98 | canvas* | background: OMT 'Canv' chunk named `<nodeName>background` (or the 3rd ctor argument), loaded via `0x44dca0('Canv', …)`, installed with SetCanvas `0x45cd30` |
| 0x9C | u16 | input-suppression flag: when set, ListenToMessage does not forward mouse messages |
| 0xA0 | obj* | mouse relay (`new(0x2C)` `0x492cb0`): converts OMT mouse events into the -600/-601/-602 messages below; the node listens to it |
| 0xA4 | int | debug display active; +0xA8 debug canvas, +0xAC/+0xB0/+0xB4 debug text objects, +0xB8..+0xC0 debug state (registry `Debug Display`, font "Lucida Console 10", `debug.omt`) |

Globals owned by HCNode.cpp (shared by all nodes, so anims/scripts outlive a node unless deleted):
`0x80a410` anim map (std::map<name, HC2DAnim*>, case-insensitive), `0x80a420` script map (name → HCScript*),
`0x80a430` deferred-delete list of anims, `0x80a43c` the **script-event broadcaster** (created by
HCApplication init; every running script listens to it, see [scripts.md](scripts.md)), `0x80a438` render
suppression flag, `0x80a40c` log anim creation times, `0x80a388` memory logging flag, `0x80a384` the app.

### HCNode main vtable (`0x7f45f8`, 50 slots) and slot semantics

The application drives a node through these slots (callers in brackets):

| slot | off | HCNode impl | name / semantics |
|---|---|---|---|
| 0 | +0x00 | `0x454860` | scalar deleting dtor (real dtor `0x454880`) |
| 1 | +0x04 | `0x45da80` | FindElement(id) — world element search by id (+0x68 of element) |
| 2 | +0x08 | `0x45c810` | NewLayer(arg) — `new(0x38)` `0x494a30(this,arg)` |
| 3 | +0x0C | `0x45cf60` | Tick — advance the world timer, `IsTimeToRender` (slot 7) → Render(0). Logo nodes override with `0x4211b0` |
| 4 | +0x10 | `0x456d50` | **Render(bool)** — skipped while `0x80a438`≠0; updates the debug display (slot 49) then world render `0x45cff0` (each layer's +0x14/+0x18 hooks); exceptions are logged ("Exception caught in HCNode::draw()") |
| 5 | +0x14 | `0x45cd70` | SetTimeMode(int) (+0x30, resets elapsed) |
| 6 | +0x18 | `0x45cdb0` | SetFrameInterval(int) (+0x34, resets elapsed) |
| 7 | +0x1C | `0x45cdf0` | IsTimeToRender() |
| 8 | +0x20 | `0x45c880` | world render hook (also reached through the +0x6C table) |
| 9 | +0x24 | `0x45cd30` | SetCanvas(obj) — replaces +0x68 (deleting the old one), then Invalidate |
| 10 | +0x28 | `0x45cd60` | Invalidate() (+0x4C = 1) |
| 11 | +0x2C | `0x454bc0` | **Pause(bool)** — world pause count (+0x48) then posts script events 0x66 (paused) / 0x67 (resumed) on the 0↔1 transitions. App calls Pause(1) on the old node before a switch |
| 12–17 | +0x30..+0x44 | `0x45d1d0`, `0x45cee0`, `0x45d080`, `0x45d3b0`, `0x45d0e0`, `0x45c890` | world internals (add element, frame-time averaging, update/pause element ranges, …) |
| 18 | +0x48 | `0x454c10` | **Start()** — called by the app (`0x44b4e0`) after constructing the node and fading in. Default posts 0x69 + node name |
| 19 | +0x4C | `0x49d710` (no-op) | **Exit(const char* nextNode)** — called by the app before deleting the node. HCToolbarNode: `0x41e8d0` → music selector `0x4133e0(nextNode)` |
| 20 | +0x50 | `0x49d720` (no-op) | **Quit()** — app quit (`0x44b2e0`, omsg_Quit −100) |
| 21 | +0x54 | `0x454c40` | **MouseDown(HCMouseEvent*)** — from the mouse relay (−600) via ListenToMessage; the normal click handler. Default posts script event 0x64 when the hit kind (event+0) is 2 |
| 22 | +0x58 | no-op | **MouseUp(HCMouseEvent*)** (−601) |
| 23–26 | +0x5C..+0x68 | no-op | AppLeftDown / AppLeftUp / AppRightDown / AppRightUp(event*) — raw app events (OMT −102 type 1/3, −103 type 2/4) from `0x44be50`; never overridden |
| 27 | +0x6C | no-op | **KeyDown(event*)** (OMT −104) |
| 28 | +0x70 | `0x454c70` | **KeyUp(event*)** (−105); default: key event (type 6) with modifier bits 4 and 8 → memory dump `0x49d720(0)` *(inferred)* |
| 29 | +0x74 | `0x458cf0` no-op | AnimSequenceStarted(HC2DAnim*, int seq) — from `HC2DAnim::SetSequence` `0x449540` |
| 30 | +0x78 | `0x454ca0` | **AnimEnded(HC2DAnim*, int)** — from `0x449540`/`0x449660` when a sequence finishes; default posts 0x6C + anim name (anim+0x100) |
| 31 | +0x7C | `0x458d00` no-op | AnimFrameCue(HC2DAnim*, int cue, int) — frame-embedded cue; HCToolbarNode `0x41c270` implements cue 999 "jump": move the anim by the cue's (dx,dy) and set sequence |
| 32 | +0x80 | `0x457010` | **SoundEnded(const char* sound, bool looping)** — from sound Stop `0x44f7b0`; default posts 0x6A |
| 33 | +0x84 | `0x456fe0` | **MovieEnded(const char* movie)** — from HCMovie `0x453a20`; posts 0x6B |
| 34 | +0x88 | no-op | **ScriptStarted(const char* script)** — from StartScript `0x455b40` |
| 35 | +0x8C | `0x457040` | **ScriptPreempted(const char* script)** — script stopped before its end (`0x4595f0`); posts 0x6D |
| 36 | +0x90 | `0x457040` | **ScriptEnded(const char* script)** — script ran to completion (`0x459510`/`0x459580`); posts 0x6D |
| 37 | +0x94 | `0x458cf0` no-op | ScriptNotify(const char* script, int) — script opcode callback `0x458e90` |
| 38 | +0x98 | `0x458d00` no-op | **ScriptCommand(const char* script, const char* cmd, int)** — script opcode `0x458e50` calls the node by command name (e.g. HCToolbarNode handles "initDelay", "initDelayVoice", "voice…", "SayHorseName") |
| 39–46 | +0x9C..+0xB8 | `0x401b80`/`0x401ba0`/`0x40bd60`/`0x401b90` (no-ops, 1–4 args) | unused hooks (never overridden) |
| 47 | +0xBC | `0x455e10` | CreateDebugDisplay (registry `Debug Display`≠0) |
| 48 | +0xC0 | `0x456970` | DestroyDebugDisplay |
| 49 | +0xC4 | `0x4569f0` | UpdateDebugDisplay (frame rate, free memory text) |

Slots 50–54 are added by HCToolbarNode (below). Some nodes add further class-specific virtuals at +0xDC….

### Messages and the mouse event

The mouse relay (`0x492db0`) turns OMT mouse button messages (−102 down / −103 up) into a 0x24-byte
`HCMouseEvent` on the stack and broadcasts −602 (hit test by the layer), then **−600** (down; only if the hit
test found something) or **−601** (up; always) to the node's listener. `HCNode::ListenToMessage` (`0x454b50`)
forwards −600 → slot 21 MouseDown and −601 → slot 22 MouseUp unless +0x9C is set or the hit kind
(`event+0`) is 1, then passes the message to the world listener `0x45d6b0`.

| off | type | HCMouseEvent field |
|---|---|---|
| 0x00 | u32 | hit kind (0 = nothing hit; filled by the −602 hit test; 2 = an element was hit, 1 = not forwarded to the node *(inferred)*) |
| 0x08 | HC2DAnim* | element/anim under the cursor (toolbar buttons are matched by this pointer) |
| 0x14/0x16 | s16 | x, y relative to the node layer (screen − layer origin +0x28/+0x2A) |
| 0x18/0x1A | s16 | copy of x, y |
| 0x1C | u16 | modifier flags (OMT event +4) |
| 0x1E | u16 | 1 when not the left button |
| 0x20 | u32 | raw OMT event type: 1 left down, 2 left up, 3 right down, 4 right up |

Script events posted by the HCNode defaults to `[0x80a43c]` (`vtbl[1](code, data)`; this is what script
`wait…` opcodes wake on — semantics in [scripts.md](scripts.md)): 0x64 MouseDown, 0x66/0x67 paused/resumed,
0x68 node tick (SpendTime, data = node), 0x69 Start (node name), 0x6A SoundEnded, 0x6B MovieEnded,
0x6C AnimEnded (anim name pointer, sequence), 0x6D ScriptEnded/ScriptPreempted. **An override that does not
chain to the HCNode default therefore stops scripts from seeing that event.**

### Node life cycle (driven by HCApplication, `0x44b4e0`)

1. `app->vtbl[0x44]("name", 1)` (`Godiva::GotoNode` `0x413370`) saves the Miles master volume (`0x7ffa64`), clears app+0x34C and queues the next node name (`0x44de80` → app+0xF0); nothing happens until the current event/idle handler returns.
2. On the next main-loop pass: old node `Pause(1)` → fade out (`0x44df20(1,…)`) → old node `Exit(nextName)` →
   `delete node` → close the node's `.omt` → open `<nextName>.omt` (`0x44d510`) → `app->vtbl[0x20](name)` =
   `Godiva::CreateNewNode` (`0x412140`) → fade in (`0x44df20(0,…)`) → `0x44b3c0` discards queued keyboard (WM 0x100–0x108) and mouse (0x200–0x209) window messages → new node `Start()`.
3. Every main-loop pass runs the node's SpendTime (+0x0C table); HCNode::SpendTime (`0x456f30`) ends in the world idle
   `0x45cf50`, which advances the timer and renders when the frame interval has elapsed (see [application.md](application.md)).
4. HCNode dtor (`0x454880`): stops/deletes the anims owned by this node, deletes all scripts, flushes the
   deferred anim deletions, deletes the mouse relay, frees the name.

### HCNode non-virtual methods

| address | name | purpose |
|---|---|---|
| `0x454320` | HCNode(app, name, bgName=0) | see layout; creates layer, background canvas, mouse relay; optional debug display |
| `0x454880` | ~HCNode | see life cycle |
| `0x454ad0` | LoadCanvas(name) | `new(0x14)` `0x493600`, filled from OMT resource `name` (`0x44dd90`) |
| `0x454ce0` | CreateAnim(name, instanceName=0, noAttach=0, p4) → HC2DAnim* | returns the existing anim of that name (re-attached to the current node: owner, add to world, position (0,0), sequence 0) unless an instance name is given; otherwise `new(0x150)` `HC2DAnim(app,name,0,noAttach,p4)` (`0x447a00`), optional rename (+0x100 name/+0x104 len), insert into the anim map. Logs "Anim %s created in %ld milliseconds" |
| `0x455020` | FindAnim(name) | anim-map lookup (case-insensitive) |
| `0x4550b0` | DeleteAnim(name) | hide (vtbl+0x30(1)), remove from map, queue for deletion |
| `0x455180` | DeleteAnim(HC2DAnim*) | by the anim's own name |
| `0x4551b0` | DeleteAllAnims | |
| `0x4552d0` | FlushDeletedAnims | deletes the queued anims (called from SpendTime and the dtor) |
| `0x455890` | RunScript(name) | LoadScript + StartScript |
| `0x4558b0` | LoadScript(name) | logs "ScriptStarted - %s"; `new(0x70)` `HCScript(name,this)` (`0x458fb0`) from the node's .omt ("SCR " chunk), inserts into the script map (reuses an existing one) |
| `0x455b40` | StartScript(name) | ScriptStarted callback, then runs the first slice `0x459510` |
| `0x455840` | IsScriptRunning(name) | exists and not finished (+0x52) |
| `0x455bb0` | StopAllScripts | each → `0x4595f0` (ScriptPreempted callback) |
| `0x455c10` | StopScript(name) | |
| `0x455cb0` | GetScriptVar(script, var, out) | `0x459690` |
| `0x455d60` | SetScriptVar(script, var, double) | `0x4596b0` |
| `0x455730` | DeleteAllScripts | |
| `0x456f30` | SpendTime (periodical slot 1) | deletes finished scripts flagged +0x52, posts 0x68, then world periodical `0x45cf50` |
| others | `0x454a10`/`0x454a70`/`0x455ae0` map iterators, `0x455340`/`0x457070`/`0x457480`/`0x457510`/`0x457880`/`0x457c90`/`0x457f90`/`0x457f50`/`0x458250`/`0x458290`/`0x4582f0`/`0x4577a0`/`0x457820` std::map (red-black tree) internals for the anim and script maps, `0x4553d0`…`0x455590` script map static init/atexit, `0x4568c0` SetDebugText, `0x456dbb` Render's catch block, `0x458350`…`0x458380` dtor thunks |

## HCToolbarNode

Base of every interactive screen (all factory nodes except the three logo nodes). Adds the toolbar
(Quit, Pager, Camera, Map, Bag, Help, Home buttons), toolbar popups (pager, map, saddlebag), the exit
dialog, Barbie's voice with mouth-flap animation, a mouse cursor anim, edge scrolling of wide panoramas,
helper script commands and the node-name table. Ctor `0x41b690(app, nodeName)` → `HCNode(app, nodeName, 0)`.
Dtor `0x41bc30`; scalar deleting `0x41bc10`; thunks `0x41e950` (+0xC), `0x41e960` (+0x18), `0x41e970`
(+0x6C), `0x41e980` (+0x7C). Vtables: main `0x7f1b18`, +0xC `0x7f1b10`, +0x18 `0x7f1b00`, +0x6C `0x7f1af8`,
+0x7C `0x7f1ae8`. Object size 0x1C0 (derived node fields start at 0x1C0).

### Construction

1. HCNode part; resets all toolbar fields (table below); +0x160 = 640 (pan limit), +0x1B4 = 1, +0x1B8 = app.
2. +0xFA = `display->vtbl[0x3C]() != 0` (selects screen vs layer mouse coordinates).
3. Forces the layer's renderer (+0xA8 object) into mode 3.
4. Once per process (global `0x809fa8` == 0): opens `commonin.omt.cache` and `commonout.omt.cache`
   (`0x44d510(name,1,1)`) and pre-creates 34 shared toolbar anims into globals `0x809fa8..0x80a02c`, hiding
   each (vtbl+0x1C): `christieLCDIn, pagerIn, pagerNoIn, pagerRaceIn, pagerReplayIn, pagerYesIn, tbQuitIn,
   teresaLCDIn, cursorIn, saddlebagIn, sbFlapIn, tbBagIn, tbCameraIn, tbHelpIn, tbHomeIn, tbMapIn, tbPagerIn`
   and the same 17 names with `Out`. "In" = indoor skin, "Out" = outdoor skin.
5. Resets the app's idle/help timer: `app+0x19C` object `vtbl[0x18]()`.

Derived constructors then set +0x108 (skin; mini-games also +0xCE/+0xD0), call `SetCursor("cursor"+skin)` and
`AddToolbarButton("tb…"+skin, id)` for each button they show.

### Fields (this+)

| off | type | meaning |
|---|---|---|
| 0xC4 | u16 | edge-scroll enable (left and right) — cursor arrows at x<15 / x>604, posts 13000 |
| 0xC6/0xC8/0xCA/0xCC | u16 | further edge/arrow cursor modes (messages 0x32C9 left, 0x32CA right, 0x32CC right-only) |
| 0xCE | u16 | "mini-game" flag (set by applesort, leapfrog, vinegame): Pager/Map/Bag clicks leave the mini-game instead of opening a popup — see Toolbar buttons |
| 0xD0 | int | return location for a mini-game, as an index into the node-name table `0x7f1f50` (applesort 0x11 appleW, leapfrog 0x13 bridgeS, vinegame 0x15 rainbowS). Not initialised by the base ctor; only read when +0xCE is set |
| 0xD4 | BarbieSaddlebag* | created by AddToolbarButton(id 4) — `new(0x30)` `0x406bb0(this)` |
| 0xD8 | BarbiePager* | created by AddToolbarButton(id 1) — `new(0x70)` `0x405a70(this)`; pager+0x24 = its button anim |
| 0xDC | list* | toolbar button list (HCRolloverButton objects, ctor `0x40ec40`, `new(0xC8)`); +0xE0 count |
| 0xEC | HC2DAnim* | cursor anim (SetCursor) |
| 0xF0 | popup* | active popup (pager+0xC, map, saddlebag); popup vtable: +4 Open, +8 Close, +0xC MouseDown→nonzero closes |
| 0xF4 | int | active popup kind: 0 none, 1 pager, 3 map, 4 saddlebag |
| 0xFA | u16 | mouse coordinates are already layer-relative |
| 0x108 | int | toolbar skin: 0 "In", 1 "Out", 2 "Frog" (leapfrog), 3 "Sign" (sign-in), 4 "Vine" — `0x41d150` returns the suffix |
| 0x114 | u32 | tick of the last scroll-velocity change |
| 0x158/0x15C | float | panorama scroll velocity x/y (`0x41cb40`) |
| 0x160/0x164 | int | panorama right limit (640) / left limit |
| 0x168/0x16C | float | current mouse position (layer coordinates) |
| 0x170 | u16 | close-pager-when-idle request |
| 0x178 | char* | current voice sound name; +0x17C u16 voice playing |
| 0x180/0x184 | char*/int | delayed voice (DelayVoice) and its delay (ms) |
| 0x188/0x18C | u16/HC2DAnim* | mouth-flap anim active / the flap anim |
| 0x190 | int | delay (ms) used by the "Delay" script |
| 0x194 | u16 | skip the next photo: the camera button only plays the shutter sound and clears the flag |
| 0x19C | u16 | exit dialog showing; +0x1A0 exit_bg panel, +0x1A4 exitborder anim, +0x1A8/+0x1AC/+0x1B0 Yes/No/Credits rollovers |
| 0x1B4 | u16 | pager click enable: 1 after the ctor; when 0, one pager click is swallowed and the flag is re-armed |
| 0x1B8 | Godiva* | the application (holds the player record at +0x1B0) |
| 0x1BC | u16 | "go to rabbitE when DB9011 ends" latch |

### Toolbar buttons

`AddToolbarButton(animName, id)` (`0x41bf40`) creates an HCRolloverButton on `animName`, stores the id in the
anim's element id (+0x68), positions it at the layer origin with depth 3000.0, and appends it to +0xDC. Id 1
also creates the pager, id 4 the saddlebag (and restores its items from the record), id 5 sets the anim's click id
(+0x11C) to 0x32CE.

| id | anim (In/Out) | action in `OnToolbarButton` (`0x41c530`; app+0x1AC is cleared first) |
|---|---|---|
| 0 | tbQuit | `ShowExitDialog(1)` (save on exit) |
| 1 | tbPager | OpenPopup(1) (pager), unless +0x1B4 is 0 (then only re-arm it). Mini-game (+0xCE): app+0x1AC=1, app+0x33C=1, `GotoNode(nameTable[+0xD0])` — the trail node's HorseStop (`0x432f30`) opens the requested popup on arrival and clears app+0x1AC |
| 2 | tbCamera | `TakePhoto` (`0x41c840`) on left-button-up: sends message 0x32CD to its own ListenToMessage (the trail missions C1–C3 react to it), then unless +0x194 is set: hide toolbar, `BarbieCamera::TakePhoto(app+0x3C)` (`0x404200`), shutter sound "camera" if a photo slot was free, show toolbar |
| 3 | tbMap | horse chosen (app+0x1C0≠4) → OpenPopup(3) (the BarbieMap at app+0x188); no horse → `GotoNode("stable")`. Mini-game: app+0x1AC=3, app+0x33C=1, return to +0xD0 |
| 4 | tbBag | OpenPopup(4) (saddlebag). Mini-game: app+0x1AC=4, app+0x33C=1, return to +0xD0 |
| 5 | tbHelp | not handled here: derived nodes handle id 5 in their own MouseDown (anim +0x11C = 0x32CE) |
| 6 | tbHome | horse chosen → `GotoNode("tackroom")`; else `GotoNode("stable")` unless already there |

`MouseDown` (`0x41c340`, also used for MouseUp `0x41c450`): with the exit dialog up, element ids 0x32CF (Yes:
close popup, QuitGame), 0x32D0 (No: delete the dialog), 0x32D1 (Credits: `GotoNode("credits")`); otherwise a
click goes to the active popup (`0x41c810`) or to `OnToolbarButton`.

`ListenToMessage` (`0x41c910`): routes −600 clicks to an open popup (closing it when its handler returns 0);
on left-button-up near the screen edges posts scroll messages to itself (13000 / 0x32C9 / 0x32CA / 0x32CC)
depending on the scroll flags.

`SpendTime` (`0x41cba0`): reads the mouse, converts it to layer coordinates; if no popup is busy and the scroll
velocity is non-zero, pans the layer (`x += (now − last) × 0.01 × velocity`, clamped to [+0x164, +0x160 − width])
and moves the buttons with it; selects the cursor arrow frame (0 normal, 1–6 arrows) from the edge flags and
x<15 / x>604, keeps the cursor inside 640×480; closes the pager when requested; polls the voice
(`0x41d3f0`) while one is playing; then HCNode SpendTime.

### HCToolbarNode virtual slots (main table)

| off | impl | name |
|---|---|---|
| +0x00 | `0x41bc10` | scalar deleting dtor |
| +0x4C | `0x41e8d0` | Exit(next) → node music `0x4133e0(next)` |
| +0x54 / +0x58 | `0x41c340` / `0x41c450` | MouseDown / MouseUp |
| +0x6C | `0x41c310` | KeyDown: key code 1 (event+8; Esc *(inferred)*) → ShowExitDialog(1) |
| +0x78 | `0x41c250` | AnimEnded (→ HCNode) |
| +0x7C | `0x41c270` | AnimFrameCue (cue 999 = jump) |
| +0x80 | `0x41c460` | SoundEnded: "DB04158" → `GotoNode("cottageL")`; "DB9011" with +0x1BC → `GotoNode("rabbitE")` |
| +0x90 | `0x41dc80` | ScriptEnded: "DelayVoice" → play the delayed voice |
| +0x98 | `0x41dcd0` | ScriptCommand: "initDelay" → set script var "delay"=+0x190, "initDelayVoice" → "delay"=+0x184, "voice…" → mouth flaps, "SayHorseName" → slot +0xC8 |
| +0xC8 | `0x41e720` | **SayHorseName()** — voice "DB0%d" of the current horse's name |
| +0xCC | `0x41e630` | **QuitGame()** — DesktopHorse copy (`0x411430`), shortcut create/delete by app+0x348, `0x412db0(app+0x34A)` = save if requested, quit |
| +0xD0 | `0x41e150` | **ShowExitDialog(bool save)** — app+0x34A = save; exit_bg panel, exit_yes/no/credits rollovers, exitborder anim, voice "db0577" |
| +0xD4 | `0x41dda0` | GetMouseWorldPos(float* out) |
| +0xD8 | `0x41de40` | GotoNodeById(id): 7 tackroom, 17 appleW, 19 bridgeS, 20 rabbitE, 21 rainbowS, 22 beachE |

Secondary: +0xC `0x41cba0` SpendTime; +0x18 `0x41c910` ListenToMessage.

### Other HCToolbarNode methods

| address | name | purpose |
|---|---|---|
| `0x41beb0` | SetCursor(name) | hide old cursor, create the new one, ensure visible, flags+0x62=1, clear scroll flags |
| `0x41c110` | AllocRolloverListNode | pooled list node allocator |
| `0x41c190` | ShowToolbar(show, redraw) | per button `0x40f240`, cursor visibility, optional Render |
| `0x41c1f0` | MoveRollovers(x, y) | reposition visible buttons |
| `0x41c4f0` | FindButtonById(id) | |
| `0x41c760` | OpenPopup(kind) | 1 pager, 3 map, 4 saddlebag; calls popup Open |
| `0x41c7e0` | ClosePopup | popup Close, +0xF4 = 0 |
| `0x41c810` | PopupMouseDown(event) | forwards; closes when handled |
| `0x41cb40` | SetScrollVelocity(vx, vy) | |
| `0x41d0e0` / `0x41d100` | CreateAnim / CreateAnimAtViewport(name, inst) | the latter positions at the layer origin |
| `0x41d1a0` / `0x41d200` / `0x41d220` | AddBagItem(item, quiet) / HasBagItem(item) / RemoveBagItem(item) | saddlebag wrappers (`0x407b40`/`0x408310`/`0x4084e0`; item ids in BarbieSaddlebag); item 24 (coin) also sets/clears app+0x2AC; Add plays "bagclose" |
| `0x41d250` | RunDelay(ms) | +0x190 = ms, RunScript("Delay") |
| `0x41d270` | PlayVoice(name) | music (channel 13) volume → 0x18; current voice volume (0x20, pan 0x40); PlaySound(name, ch 14, queue 0, preempt 1, loops 1, keep 0, vol 0x70); +0x17C=1; mouth flaps on |
| `0x41d330` | PlayVoiceNoFlaps(name) | same ducking, but PlaySound(name, ch 14, queue 1, preempt 0, …) — queued behind the current voice, no flaps |
| `0x41d310` | PlayVoiceAfterPause(name) | "pause" then name |
| `0x41d3c0` | DelayVoice(name, ms) | RunScript("DelayVoice") |
| `0x41d3f0` | PollVoiceDone | when channel 14 is idle: music volume back to 0x30, flaps off, +0x17C=0 |
| `0x41d480` | StopVoice | |
| `0x41d510` | UpdateMouthFlaps(on) | picks the flap anim matching Barbie's current anim/sequence ("ambientBarbie"→"mountFlaps" in yard, "barbie ambients"→"walkFlaps", "BarbieIntro", "gameBarbie"→"gameFlaps" in bellgame, …) |
| `0x41d860` | StopMusic | channel 13 |
| `0x41d870` | ReleaseVoice | |
| `0x41d8b0` | SayHorseNameThen(name) | horse-name voice then `name` |
| `0x41d980` / `0x41db70` | GetCollision(animA, animB) / variant | bounding-box overlap of two anims ("GetCollision was passed a NULL Anim!") |
| `0x41df10` | ChoosePagerMessage | pages the player through BarbiePager::PageMessage (`0x406970`): at random either a friend chat (0x3415 Christie / 0x3416 Teresa, biased by the horse) or a rescue offer for a badge not yet earned — 0x3417 R1 bird (rec+0xDC), 0x3419 R2 horseshoe (rec+0xD8), 0x3418 R3 branches (rec+0xE0) |
| `0x41e670` | GetHorseNameSound | "DB0%d" for the current horse name id (static buffer `0x809f70`) |
| `0x41e7e0` | CheckAllMissionsDone | all 8 mission flags app+0x258..0x274 set and app+0x2F8==0 → app+0x310=15, app+0x2F8=1, voices "pause","pause","DB04157", open saddlebag, "DB04158" (its end → cottageL) |
| `0x41e8c0` | IsVoiceChannelBusy | |
| `0x41e8f0` | PlayHelpHint | escalating help: app+0x2FC = 0 → "DB03006"+"DB9054a", 1 → "DB9055a", else "DB9056"; increments |

Horse-name voice numbers (`0x41d8b0`/`0x41e670`/`0x41e720`): id = app+0x1C8[horse]; 10 → 0x807, 0x24 → 0x806,
0x25..0x27 → 0x29C..0x29E, other id <11 → id+0x277, ≥11 → id+0x278; sound name `"DB0%d"`.

### Node-name table (`0x7f1f50`, char[0x80] entries)

0 applesort, 1 bulletinboard, 2 intro, 3 signin, 4 leapfrog, 5 stable, 6 race, 7 tackroom, 8 yard, 9 vinegame,
10 preferences, 11 grooming, 12 carousel, 13 bellgame, 14 opendoor, 15 finalpayoff, 16 intromovie, 17 appleW,
18 cottageL, 19 bridgeS, 20 rabbitE, 21 rainbowS, 22 beachE, 23 wishingL, 24 credits, 25 EXIT, 26 BOOK,
27 CAMERA, 28 MAP, 29 BAG, 30 HELP, 31 PRINT.

## Godiva (application object)

Game application object: owns the player record, the node factory, node music, idle timer, photo/map helpers.
Created by Godiva::Main 0x413090 (`new(0x350)`, ctor 0x411bd0). Global pointer [0x80a384] = app (set by the
HCApplication ctor).

- ctor 0x411bd0, dtor 0x412080, scalar deleting dtor 0x411c50.
- Vtables: main 0x7f1600 (18 slots); +0x20 listener 0x7f15f0 [0x413910 thunk→ListenToMessage, 0x413920 thunk dtor,
  0x45bd80] (base 0x7f41f0 [0x450060, 0x450070, 0x45bd80]); +0x30 periodical 0x7f15e8 [0x413930 thunk dtor,
  SpendTime 0x412060] (base 0x7f41e8 [0x450080, HCApplication::SpendTime 0x44b4b0]).
- Assets: `commonhd.omt`, `common.omt` (opened in Start, closed in dtor); music via PlayMusic (channel 13).

### App vtable 0x7f1600 vs HCApplication 0x7f4200

| slot | HCApplication | Godiva | meaning |
|---|---|---|---|
| +0x00 | 0x44be50 | **0x413020** | ListenToMessage(msg,param) — base dispatches −0x66/−0x67 buttons, −0x68/−0x69 keys, −0x6C sound, −0x65, −100 quit, −300 low memory to current node |
| +0x04 | 0x44a630 | **0x411c50** | scalar deleting dtor |
| +0x08 | 0x46d5d0 | 0x46d5d0 | OMT SetSupervisor(sup) (app+0x4) |
| +0x0C | 0x44b2d0 (→0x47a970) | **0x411c70** | Start/Run: base = OMT main loop until app+0x14 (do_quit) |
| +0x10 | 0x49d710 | same | stub (1 arg) *(unknown)* |
| +0x14 | 0x44aa30 | same | Initialize: window/screen, Miles sound (`Sound` setting), broadcaster registration |
| +0x18 | 0x44b160 | same | CheckDisc: looks for `<app+0xe4>common.omt`, retry/cancel dialog; 1 = found |
| +0x1C | 0x44de80 | same | RequestNodeChange(name, fadeSteps≤16, flag) → app+0xf0 name, +0xfc steps, +0x102 flag, +0xf8=1 |
| +0x20 | purecall | **0x412140** | CreateNewNode(name) |
| +0x24 | 0x44df10 | same | SetCurrentNode(node) (app+0xec) |
| +0x28/+0x2C | 0x49d720 | same | empty hooks |
| +0x30 | purecall | **0x411c20** | GetCompanyName → "Mattel" |
| +0x34 | purecall | **0x411c30** | GetAppName → "Barbie ® Riding Club" (window title, mutex name) |
| +0x38 | purecall | **0x411c40** | GetVersion → "1.0" |
| +0x3C | 0x44b2e0 | same | Quit: quitting flag [0x80a438], node->Quit(), app+0x14=1 |
| +0x40 | 0x49d710 | same | stub (1 arg) |
| +0x44 | — | **0x413370** | GotoNode(name, int) (new in Godiva; second argument ignored) |

### Node factory — Godiva::CreateNewNode 0x412140 (case-insensitive compare, OutputDebugString per node)

| name | ctor | new() size | notes |
|---|---|---|---|
| MattelLogoNode | 0x420f60 | 0xd0 | (app, name, "HCLogoNode", 15000) |
| HCLogoNode | 0x4194f0 | 0xd8 | (app, name, "BarbieSoftLogoNode", 15000) |
| BarbieSoftLogoNode | 0x40a080 | 0xd0 | (app, name, "preferences", 15000) |
| preferences | 0x4216d0 | 0x1fc | |
| signin | 0x427370 | 0x25c | |
| stable | 0x429d90 | 0x25c | |
| tackroom | 0x42c0b0 | 0x2a0 | |
| bulletinboard | 0x40def0 | 0x1f8 | |
| grooming | 0x413940 | 0x258 | |
| carousel | 0x40f860 | 0x224 | |
| applesort | 0x4015e0 | 0x270 | |
| leapfrog | 0x41f1f0 | 0x270 | |
| yard | 0x43cda0 | 0x214 | |
| vinegame | 0x43c0c0 | 0x28c | |
| bellgame | 0x40a980 | 0x25c | |
| opendoor | 0x421200 | 0x1c4 | |
| finalpayoff | 0x411810 | 0x1c4 | |
| intromovie | 0x41e990 | 0x1c8 | |
| race | 0x422d00 | 0x284 | |
| appleW | 0x40a2f0 | 0x7fc | trail node |
| cottageL | 0x40a450 | 0x7fc | trail node |
| bridgeS | 0x40a500 | 0x7fc | trail node |
| rainbowS | 0x40a660 | 0x7fc | trail node (compared twice; 2nd branch dead) |
| rabbitE | 0x40a5b0 | 0x7fc | trail node |
| beachE | 0x40a3a0 | 0x7fc | trail node |
| wishingL | 0x40a710 | 0x7fc | trail node |
| credits | 0x410d20 | 0x1c8 | |
| (no match) | 0x43cda0 | 0x214 | logs "no match" and creates a **yard** node |

Trail/credits names are taken from the node-name table 0x7f1f50 (entries 17–24). No factory entry for intro,
EXIT, BOOK, CAMERA, MAP, BAG, HELP, PRINT.

### Node music — Godiva::SelectNodeMusic 0x4133e0 (called by HCToolbarNode::Exit 0x41e8d0 with the next node)

Always: stop all sounds (0x44c780(1)), release cached (0x44c990(1)), stop channel 13. Then, only if app+0x344
(music off) == 0, PlayMusic (0x4133c0 = PlaySound(name, ch 13, loops 0, …, vol 0x30, stream)):
signin→`sign-in`; yard→`trailMusic`; stable, grooming, tackroom, bulletinboard→`stablemusic`; appleW, rainbowS,
beachE, bridgeS, race→`trailMusic`; rabbitE→ app+0x254 ? `trailMusic` : `nonmissionMusic`; applesort, vinegame,
leapfrog→`gamemusic`; wishingL, cottageL→`hiddenmusic`; anything else → silence.

### App fields (0x000–0x34F)

| off | type | meaning |
|---|---|---|
| +0x000 | vptr | 0x7f1600 |
| +0x004 | ptr | OMT supervisor |
| +0x014 | u16 | OMT do_quit (main loop runs while 0) |
| +0x020 / +0x030 | vptr | listener / periodical sub-objects |
| +0x03C, +0x040, +0x044 | | display/window object and flags (HCApplication::Initialize) |
| +0x048..+0x0CF | | HCApplication sound bookkeeping (list +0x48/+0x4C, 16 per-channel queues from +0x50) |
| +0x0D0 | HDIGDRIVER | Miles digital driver (GotoNode stores its master volume in [0x7ffa64]) |
| +0x0DC / +0x0E0 | char* / u16 | install directory string (Saved Games\, Photos\, DesktopHorse\) |
| +0x0E4 / +0x0E8 | char* / u16 | CD / resource directory (common.omt check, DesktopHorse source bitmaps) |
| +0x0EC | HCNode* | current node |
| +0x0F0 / +0x0F4 | char* / u16 | pending node name |
| +0x0F8 | u16 | node change pending |
| +0x0FC | u16 | fade steps (16) ; +0xFA/+0xFE/+0x100 u16 base flags; +0x102 u16 change flag |
| +0x174/+0x178/+0x17C/+0x180 | u32,u32,u32,u8 | text font LOGFONT height 10 / width 0 / weight 400 / pitch&family 0x31 (used 0x44e930) |
| +0x184 | BarbieCamera* | new(1), ctor 0x404080 (creates `<installdir>Photos\`) |
| +0x188 | BarbieMap* | new(0x58), ctor 0x404f70, vtable 0x7f01f8 (toolbar MAP popup) |
| +0x194 | u32 | debug flag: draw "Mouse (%d,%d)" each tick (0x412de0) |
| +0x19C | Timer* | idle timer (0x42fb10, 0x34 bytes), fires message 50000; reset on mouse buttons, paused/restarted by the handbook; bulletinboard sets 450000 ms |
| +0x1A4 | u32 | idle interval 20000 ms (ctor) |
| +0x1A8 | u32 | control mode for race and trail ride: 1 keyboard (preferences: keyboard), 0 mouse buttons (preferences: mouse), 2 mouse motion (not selectable) |
| +0x1AC | u32 | toolbar popup requested from a mini-game (1 pager, 3 map, 4 bag); TrailNode::HorseStop opens it on arrival and clears it |
| +0x1B0..+0x2FF | record | player record, saved as savegame<N>.brc (0x150 bytes) — see [savegame.md](../formats/savegame.md) |
| +0x300..+0x34F | | session state, not saved: tutorial flag, last race result, race course (+0x308), active mission (+0x310/+0x314), C4 progress, arrival requests (+0x33A/+0x33C/+0x33E), preferences mirror (+0x344/+0x346/+0x348), save-on-quit (+0x34A) — field table in [savegame.md](../formats/savegame.md) §2 |

### Player record defaults

`Godiva::Start` (0x411c70) initialises the in-memory record before any player is loaded (name "Barbie", horse 4,
rec+0x14 = 2, race-history table, zeroed progress fields). The field-by-field layout, the new-player defaults written by
`CreateNewSaveGame` and every reader/writer are in [savegame.md](../formats/savegame.md).

### Methods

| addr | name | purpose |
|---|---|---|
| 0x411bd0 | Godiva::Godiva | HCApplication ctor, vtables, zero +0x1AC/+0x19C/+0x184/+0x188, idle interval 20000 |
| 0x411c20/0x411c30/0x411c40 | GetCompanyName/GetAppName/GetVersion | constant strings |
| 0x411c50 | `scalar deleting destructor'` | |
| 0x411c70 | Godiva::Start | CheckDisc (fail → QuitGame(1)); font defaults; open commonhd/common.omt; build+create `<installdir>Saved Games\` (global String [0x809aa8]); BarbieCamera, photo size globals 320×240 ([0x7fe3c4]/c8, inferred), map popup, idle timer; player-record defaults; srand(GetTickCount); GotoNode("MattelLogoNode"); base main loop |
| 0x412060 | SpendTime (+0x30) | debug mouse text if app+0x194, then HCApplication::SpendTime |
| 0x412080 | ~Godiva | close common/commonhd.omt, delete timer, camera, map popup, base dtor |
| 0x412140 | CreateNewNode | factory above |
| 0x412db0 | QuitGame(short save) | save != 0 → SaveGame(app+0x1b0, [0x809ac0]); then HCApplication::Quit |
| 0x412de0 | DrawMouseDebug | prints "Mouse (%d,%d)" with viewport offset onto the screen |
| 0x412f80 | String::String(const char*) | COMDAT |
| 0x412fe0 | StartRace(msg) | 0x3415 → app+0x308=1, 0x3416 → 2; GotoNode("race") (called by BarbiePager::Close 0x406690) |
| 0x413020 | ListenToMessage | −0x67/−0x66 → idle timer vt+0x24 (restart) then base; 50000 → current node listener; else base |
| 0x413090 | Godiva::Main | new app; CreateMutex(app name); if already running FindWindow("OPEN_MEDIA_TOOLKIT_DISPLAY", name)+SetForegroundWindow; else Initialize (vt+0x14) + Start (vt+0xC); delete app; called from 0x45a4d0 |
| 0x413151 | Main catch(OMediaException&) | formats "OMT Exception: … File: … Line: …" and logs "Unhandled exception caught in om…" (Godiva.cpp line 0x242) |
| 0x4132a0 / 0x413300 | String::Append(String) / Append(const char*) | COMDAT |
| 0x413360 | OMediaException::~OMediaException *(inferred)* | frees the String at +8 of a 0x18-byte record (also used by OMT vectors) |
| 0x413370 | GotoNode(name, int) | save Miles master volume, app+0x34C=0, RequestNodeChange(name, 16, 0) |
| 0x4133c0 | PlayMusic(name) | PlaySound(name, ch 13, 0, 1, 0, 0, vol 0x30, −1, 0, stream 1) |
| 0x4133e0 | SelectNodeMusic(nextNode) | see table |
| 0x413910 | thunk +0x20 ListenToMessage | |
| 0x413920 / 0x413930 | thunk dtors (−0x20 / −0x30) | |

Transitions: Start → `MattelLogoNode`; StartRace → `race`. Save calls: QuitGame(save≠0).

## Helper classes

Classes used by several nodes. Node-specific helper classes stay with their node in nodes.md.

### Rollovers and buttons

#### Rollover

A periodical (base 0x45be00) that polls the mouse (global mouse 0x80a52c) and fires roll-in / roll-out
virtuals when the cursor enters or leaves an anim's opaque area. It is embedded at +0x10 of the 0x40ec40
hotspot helper (whose own fields start at +0xAC), so its size is about **0x9C** *(inferred)*.
- ctor 0x4269e0 `(ownerNode)`, scalar deleting dtor 0x426a40, dtor 0x426a60.
- Vtable: [0x426a40, SpendTime 0x426b90, OnRollIn 0x40ae90 (default no-op), OnRollOut 0x40ae90].
- Fields: +0x0C anim*, +0x10 owner node, +0x14 u16 enabled, +0x16 u16 currently inside, +0x18 char[] anim name
  (default "No anim initialized").

| addr | name | purpose |
|---|---|---|
| 0x426a80 | SetAnim(anim) | enables the rollover and copies the anim name (anim+0x100) |
| 0x426ad0 | SetEnabled(b) | disabling while inside fires OnRollOut |
| 0x426af0 | CheckPoint(x,y) | if the anim is visible and idle: hit test (anim vt+0x70) → OnRollIn or OnRollOut on a change |
| 0x426b90 | SpendTime | mouse position → world coordinates (adds the viewport offset unless owner+0xFA) → CheckPoint |

#### HCRolloverButton *(name inferred)*

The game's 3-state clickable button (it plays the role of an ACRollover). It wraps one HC2DAnim whose sequences are
0 normal, 1 rollover, 2 pressed, and it listens to the node's mouse relay. It only gives visual feedback: the node's
MouseDown still dispatches the click by anim id (anim+0x11C). The toolbar keeps a list of these at node+0xDC; TrailNode
disables them with SetEnabled(0). Derived classes: 0x401000 (vtbls 0x7f0010/0x7f0000; adds a 0x401160 cursor-hotspot helper at
+0xC8; 0xD4 bytes; used by every bulletin-board button) and another one in vtbls 0x7f1d30...
Bases: OMediaListener-like (0x45bbd0) at +0 and Rollover (ctor 0x4269e0, vtbl 0x7f33a0, "No anim initialized") at +0x10.
Main vtbl: 0 ListenToMessage 0x40f390, 1 sdd 0x40ece0, 2 0x45bd80, 3 Init(resName, instName) 0x40ed80.
+0x10 vtbl: 0 thunk 0x40f850 (−0x10), 1 CheckMouse 0x426b90 (tracker), 2 MouseEnter 0x40f330, 3 MouseLeave 0x40f360.

| off | type | meaning |
|---|---|---|
| +0x1C | HC2DAnim* | tracker's anim (tracker+0xC) |
| +0x20 | HCNode* | tracker's node |
| +0x24 / +0x26 | u16 | tracker enabled / mouse-inside |
| +0x28 | char[] | tracker anim name |
| +0xAC | HC2DAnim* | the button anim |
| +0xB0 | HCNode* | owner node |
| +0xB4 / +0xBC / +0xB8 | s32 | sequence for normal (0) / rollover (1) / pressed (2) |
| +0xC0 | s32 | pressed |
| +0xC4 | u16 | ctor arg 2: create the anim without the viewport offset (0x41d0e0 instead of 0x41d100) |

| addr | name | purpose |
|---|---|---|
| 0x40ec40 | ctor(node, noViewport) | init the bases and fields; AddListener(this) on node+0xA0 (mouse relay) broadcaster at +0x1C |
| 0x40ece0/0x40ed00 | sdd / dtor | DeleteAnim(+0xAC) (callers zero +0xAC first to keep the anim), base dtors |
| 0x40ed80 | Init(res, inst) | create the anim; mark the last frame of sequences 0..2 with flag 2 (hold *(inferred)*); SetSequence(normal); tracker.SetAnim; anim vt+0x38(0) |
| 0x40f240 | SetEnabled(b) | hidden = !b; tracker enabled = b (leaves if needed); enabling resets to seq 0 |
| 0x40f2b0 | SetSequence(s) | play s if the anim is not locked (+0x5C) and not hidden |
| 0x40f2f0 / 0x40f310 | Press / Release | pressed=1 → seq 2 / pressed=0 → seq 0 |
| 0x40f330 / 0x40f360 | MouseEnter / MouseLeave (this=+0x10) | enter: pressed ? seq 2 : seq 1; leave: pressed ? seq 1 : seq 0 |
| 0x40f390 | ListenToMessage(msg) | ignored if the anim is locked or hidden. −600 (down): tracker CheckMouse; inside → Press else Release. −601 (up): if pressed → Release |
| 0x40f850 | thunk −0x10 | |

Template helpers emitted here (HP-STL list with node pool 0x809adc..0x809aec, 0xFFC-byte blocks), used by Init through anim+0xC4
(a sequence table: vector<list<HCAnimFrame*>> at +4..+0xC): 0x40ee60 GetSequence(i) (grows the vector to i+1, returns &list[i]);
0x40f030 list copy-ctor; 0x40f1a0 list dtor; 0x40f400 vector::insert (realloc ×2); 0x40f600 list::insert(range); 0x40f6d0 list::erase(range);
0x40f710 list::operator=; 0x40f800 pool add-buffer.

#### ACRollover

A periodical that checks every idle tick whether the mouse is over one element. When the mouse enters, it sets
the node's cursor anim (node+0xEC) to sequence `cursorSeq` (7 = hand). When the mouse leaves, it sets sequence 0.
A live counter [0x809930] tracks instances, and HCToolbarNode's dtor warns
"you're not deleting all your ACRollovers!" when the counter is not zero.
- Base: OMediaPeriodical (ctor 0x45be00, vtable 0x7f4c28 [+0 dtor, +4 SpendTime (pure)]; dtor 0x45c030).
- ctor 0x401160(node, HC2DAnim*, int cursorSeq). Dtor 0x4011f0. Scalar deleting dtor 0x4011d0.
- Size 0xA0.
- Vtable 0x7f0020: **+0 0x4011d0**, **+4 0x401290 SpendTime**.

| off | type | meaning |
|---|---|---|
| +0x08 | u16 | periodical paused flag (base) |
| +0x0C | HC2DAnim* | watched element |
| +0x10 | node* | owner node; uses node+0x94 viewport, node+0xEC cursor anim, node+0xFA u16 coordinate mode |
| +0x14 | int | cursor sequence to show on hover (always 7) |
| +0x18 | s16 | last hover result |
| +0x1A | char[] | copy of the element's name (anim+0x100) |

SpendTime 0x401290 runs only if the element and node exist, the element is visible and the node has a viewport.
1. Gets the mouse position from the global input object ([0x80a52c]->vt+8).
2. If node+0xFA == 0, subtracts the viewport rect origin (viewport+0xAC/+0xB0).
3. Hit-tests with element->vt+0x70(viewport,x,y).
4. If the result changed, sets the cursor sequence to 0 when the mouse left, or to `cursorSeq` when the mouse
   entered, and the cursor anim has more than `cursorSeq` sequences.

The dtor resets the cursor to sequence 0, and also hides the cursor if it was hidden-counted.

##### MapVoiceRollover  *(name inferred)*
An ACRollover subclass that also plays a voice line once each time the mouse enters. It is created inline in
BarbieMap::Open (`new(0xA8)` + ACRollover ctor, then the vtable is overwritten).
- Vtable 0x7f0230: +0 0x405360 scalar deleting dtor (→ 0x405380 → jmp to ACRollover dtor 0x4011f0),
  **+4 0x405300 SpendTime**.
- Fields: +0xA0 `const char* voice`, +0xA4 u16 `armed` (starts at 1).
- SpendTime: calls ACRollover::SpendTime.
  - Not hovering: armed = 1.
  - Hovering, armed and voice set: if 0x41e8c0() returns 0 (no voice busy, *(inferred)*), sets armed = 0 and
    calls PlayVoice(voice).

#### ACButton *(name inferred)*

A clickable rollover button. It is the toolbar-style button class 0x40ec40 (OMediaListener + "Rollover"),
extended with an ACRollover that switches the cursor to the hand (cursor sequence 7) while the mouse is over it.
Other nodes use it, for example the photo album's "trashbtn" (0x416610).
- Base: class 0x40ec40 (vtables 0x7f0ee8/0x7f0ed8; see HCRolloverButton above).
- ctor 0x401000(node). Real dtor 0x4010e0. Scalar deleting dtor 0x401040, plus the +0x10 adjustor 0x401150.
- Size 0xD4 (`new(0xD4)` in 0x416610).
- Vtables:
  - main 0x7f0010: +0 0x40f390 (ListenToMessage, inherited), **+4 0x401040 dtor**, +8 0x45bd80,
    **+0xC 0x401060 Init(name,arg)** (overrides 0x40ed80).
  - +0x10 0x7f0000: **+0 0x401150 dtor thunk**, +4 0x426b90, +8 0x40f330, +0xC 0x40f360.

| off | type | meaning |
|---|---|---|
| +0xAC | HC2DAnim* | button anim (created by base Init 0x40ed80) |
| +0xB0 | node* | owner node (base) |
| +0xC4 | u16 | base flag: create through 0x41d0e0 instead of 0x41d100 |
| +0xC8 | ACRollover* | hand-cursor rollover, created in Init and deleted in the dtor |
| +0xCC | node* | owner node (ctor argument) |

| addr | name | purpose |
|---|---|---|
| 0x401000 | ACButton::ACButton(node) | calls the base ctor with flag 0, stores node at +0xCC, clears +0xC8 |
| 0x401060 | ACButton::Init(name,arg) | calls base Init (which creates the anim), then `new ACRollover(node, anim, 7)` → +0xC8 |
| 0x4010e0 | ~ACButton | deletes the rollover, then runs the base dtor 0x40ed00 |
| 0x401040/0x401150 | scalar deleting dtor (+adjustor 0x10) | |

### Toolbar popups and the camera

#### BarbiePopup *(name inferred)* — toolbar popup interface

An abstract base whose vtable 0x7f0218 is [+0 0x404fe0 scalar deleting dtor, +4 Open(node*) pure, +8 Close() pure,
+0xC MouseDown(event*) pure, +0x10 0x404fd0 HitTest(x,y,z) (default returns 1)].
- 0x405b30 is the out-of-line inline dtor (`this->vtbl = 0x7f0218`).
- The toolbar keeps the active popup at node+0xF0 and the mode at node+0xF4 (1 pager, 3 map, 4 bag).
- On a mouse event (−600) the toolbar calls HitTest and closes the popup (0x41c7e0) when it returns 0. It also
  closes the popup when MouseDown returns nonzero.

#### BarbieMap *(name inferred)* — app+0x188, popup kind 3

The trail map popup. It shows clickable locations that travel to trail nodes, overlays that hide locations not
yet unlocked, and a "you are here" marker.
- Created once in the app run function (`new(0x58)` at 0x411c70).
- ctor 0x404f70. Dtor 0x404fb0 (clears its anim pointers). Scalar deleting dtor 0x404f90.
- Vtable 0x7f01f8: +0 0x404f90, **+4 0x405000 Open(node)**, **+8 0x4059f0 Close**, **+0xC 0x405650 MouseDown**,
  **+0x10 0x405a20 HitTest**, **+0x14 0x4055f0 SetYouAreHere(int)**, **+0x18 0x405a50 IsOpen**.
- Size 0x58.

| off | type | meaning |
|---|---|---|
| +0x04 | int | you-are-here location; used as the sequence of youAreHereOut |
| +0x08 | u16 | 1 if the node palette suffix is "In" |
| +0x0C | node* | node passed to Open |
| +0x10 | HC2DAnim* | map background (`mapIn`/`mapOut`) |
| +0x14,+0x18,+0x1C,+0x20,+0x24,+0x28,+0x2C | HC2DAnim* | hotspots stable, apple, turtle, rainbow, bridge, rabbit, rabbit2 (ids 54000..54006 in that order) |
| +0x30..+0x48 | MapVoiceRollover*[7] | one per hotspot, same order. Voices: stable db9029, apple db0804, turtle db0801, rainbow db0802, bridge db0803, rabbit db0800, rabbit2 db0805 |
| +0x4C / +0x50 | HC2DAnim* | `maphideWell*` / `maphideCottage*` overlays |
| +0x54 | HC2DAnim* | `youAreHereOut` (Out palette only, otherwise 0) |

**Open(node)** 0x405000:
1. Creates the anims with suffix "In" (0x405390) or "Out" (0x405450). The "Out" set also creates youAreHereOut at
   sequence +4.
2. hideCottage is visible iff **app+0x2AA == 0**. hideWell is visible iff **app+0x2A8 == 0**.
3. Shows the background and the 7 hotspots; shows youAreHere if it exists.
4. Gives each hotspot id 54000+i and a MapVoiceRollover (`new(0xA8)`, cursor 7).
5. Caches sound `trailsign` (GetSound keep = 1) and plays sound `map`.

**MouseDown** 0x405650: acts only when event+0x20 == 1 and the id is in 54000..54006.
- If **app+0x254 == 0** (travel not allowed yet *(inferred)*): PlayVoice(db9011), node+0x1BC = 1, returns 1
  (close).
- Otherwise it plays "trailsign", hides all anims (0x405530), sets app+0x330 = 7, then:

  | id | hotspot | extra writes | GotoNodeById |
  |---|---|---|---|
  | 54000 | stable | — | **7 (tackroom)** |
  | 54001 | apple | app+0x33A = 1 | **17 (appleW)** |
  | 54002 | turtle | app+0x33A = 1 | **22 (beachE)** |
  | 54003 | rainbow | app+0x33A = 1 | **21 (rainbowS)** |
  | 54004 | bridge | app+0x33A = 1 | **19 (bridgeS)** |
  | 54005 | rabbit | app+0x33A = 1 | **20 (rabbitE)** |
  | 54006 | rabbit2 | app+0x33A = 1, app+0x342 = 1 | **20 (rabbitE)** |

  The node switch goes through node vt+0xD8 GotoNodeById. Returns 1.

**Close** 0x4059f0: deletes the 7 rollovers, hides all anims and calls ReleaseSound("trailnode"). This is probably
an original bug: Open cached "trailsign". It then clears +0x10 and +0x54.
**HitTest** tests the map background. **IsOpen** returns true if the background exists and is visible.
**SetYouAreHere(n)** stores n, sets the youAreHere sequence, and shows the marker only while the map is visible.

| addr | name |
|---|---|
| 0x405390 / 0x405450 | CreateAnimsIn / CreateAnimsOut |
| 0x405530 | HideAll |
| 0x4055b0 | DestroyRollovers (then HideAll) |

#### BarbiePager — toolbar node+0xD8, popup kind 1

The pager popup. Christie or Teresa page Barbie with a chat message or a race challenge, and the player answers
Yes, No, Replay or Race. It also blinks the toolbar pager button while a page is pending.
- Bases: OMediaPeriodical at +0 (vtable 0x7f0250: **+0 0x405b40 dtor**, **+4 0x406af0 SpendTime**) and
  BarbiePopup at +0xC (vtable 0x7f0238: +0 0x406ba0 dtor thunk (−0xC), **+4 0x4063d0 Open**,
  **+8 0x406690 Close**, **+0xC 0x4067a0 MouseDown**, **+0x10 0x406940 HitTest**).
- The popup methods run with this = P+0xC; the offsets below are relative to P.
- ctor 0x405a70(node). Dtor 0x406110. Scalar deleting dtor 0x405b40.
- Size 0x70. Created lazily by the toolbar button setup 0x41bf40 for button type 1.

**Assets:**
- Anims (In/Out suffix from node+0x108; 0 = In, 1 = Out; any other value is logged as an error and treated as Out):
  - `pager<sfx>`.
  - Buttons (class 0x40ec40, `new(200)`): `pagerYes<sfx>` id 0x237, `pagerNo<sfx>` 0x238, `pagerReplay<sfx>`
    0x239, `pagerRace<sfx>` 0x23A.
  - `christieLCDIn/Out`, `teresaLCDIn/Out` (tables at 0x7fe610 / 0x7fe618).
- Sounds: pager (beep), click.
- Voices: db04027 (first-ever pager intro), db0494/db0503 (no message), dc0495/dte3002 (chat from Christie/Teresa),
  dc0496/dt0504 (chat accepted), db0248a/db0248b (beeps).
- Race voice table at 0x7fe5d0, 5 entries per race type: [0] intro (PlayVoice), [1] question (NoFlap),
  [2] "no" answer, [3] unused, [4] "yes" answer.
  - type 0: dc0264, db0265, db0266, db0267, db0268
  - type 1: dt0248c, db0248d, db0249, db0250, db0251
  - type 2: dt0281, db0282, db0283, db0284, db0285

| off | type | meaning |
|---|---|---|
| +0x08 | u16 | periodical paused (1 = not blinking) |
| +0x10 | app* | (node+0x90) |
| +0x14 | node* | |
| +0x18 | HC2DAnim* | pager body |
| +0x1C / +0x20 | HC2DAnim* | Christie / Teresa LCD |
| +0x24 | HC2DAnim* | toolbar pager button anim (set by 0x41bf40); sequence 3 = flashing, 0 = idle |
| +0x28..+0x34 | button* | Yes, No, Replay, Race |
| +0x38 | String | name scratch |
| +0x40 | const char* | last voice line (Replay plays it) |
| +0x44 | int | LCD for the pending page (0 Christie, 1 Teresa) |
| +0x48 | int | race type 0/1/2, 3 = none (initial value) |
| +0x50 | int | open flag |
| +0x54 | u16 | one-shot "don't override the friend by app+0x1C0" |
| +0x56 | s16 | beep count |
| +0x58 | u16 | blink phase |
| +0x5C | DWORD | last blink tick |
| +0x60 | int | result to deliver on Close (accepted page id) |
| +0x64 | int | **current page**: 0 none, 0x3415 Christie chat, 0x3416 Teresa chat, 0x3417 race type 0, 0x3418 race type 2, 0x3419 race type 1 |
| +0x68 | int | previous page |
| +0x6C | u16 | "answered" (the next Yes/No/Race click closes) |

Global [0x809968] u16[3] holds the per-race-type "declined" flags (not saved).

**PageMessage(lcd, page)** 0x406970: called by the toolbar 0x41df10 and by 0x434f60.
- Sets +0x44 = lcd and +0x64 = page.
- Sets the toolbar button to sequence 3, unpauses the periodical and resets the beep count.
- 0x41df10 chooses pages from the race-completion flags app+0x28C (type 0), app+0x288 (type 1) and app+0x290
  (type 2). They are the badge counters rec+0xD8..+0xE0 (savegame.md).

**SpendTime** 0x406af0: every 1000 ms calls Blink(!phase) (0x4069b0):
- Beep 1: "pager" sound and NoFlap db0248a.
- Beep 3: "pager" sound, and PlayVoicePaused(db0248b) unless the pager popup is open (node+0xF4 == 1).
- After the 3rd beep calls StopBlink 0x406aa0 (paused = 1, button back to sequence 0).

**Open** 0x4063d0: sets open = 1 and result = 0, shows the pager (0x406250), prev = page. Then by page:
- **0x3417 / 0x3419 / 0x3418:** race type = 0 / 1 / 2. Calls ShowLCD(+0x44) and StopBlink, sets
  voice = table[type][0], plays PlayVoice(table[type][0]) and NoFlap(table[type][1]).
- **0x3415 / 0x3416 (chat):** StopBlink. friend = Teresa if the page is 0x3416. Unless the one-shot is set, the
  friend is forced to Teresa when **app+0x1C0 == 1** and to Christie when **app+0x1C0 == 0**.
  - Intro: if **app+0x2F0 == 0**, plays PlayVoice(db04027) and pause×2, then app+0x2F0++. Otherwise StopVoice.
  - Christie: page = 0x3415, NoFlap(dc0495), LCD 0. Teresa: page = 0x3416, NoFlap(dte3002), LCD 1.
- **0 (nothing pending):** StopBlink. friend = rand&1, with the same app+0x1C0 override and the same
  db04027/app+0x2F0 intro. Christie: page = 0x3415, NoFlap(db0494). Teresa: page = 0x3416, NoFlap(db0503).
  The LCD is set accordingly.

**MouseDown** 0x4067a0 (event+0x20 == 2):
- **Yes (0x237) / Race (0x23A):**
  - If already answered: clears answered, StopVoice, returns 1.
  - Otherwise plays "click". For a chat page plays dc0496 (Christie) or dt0504 (Teresa). For a race page sets
    declined[type] = 0 and plays table[type][4]. Entry [3] is therefore unreachable.
  - Sets result = page and page = 0.
- **No (0x238):**
  - If already answered: closes, as above.
  - Otherwise plays "click". On a chat page it calls StopVoice and returns 1. On a race page it sets
    declined[type] = 1 and plays table[type][2].
- **Replay (0x239):** PlayVoice(+0x40).
- After a Yes/Race/No answer: node+0x170 = 1, answered = 1. Returns 0, except where noted.

**Close** 0x406690: if open: open = 0, Hide (0x4061f0), StopVoice. Then by result r:
- r = 0x3415 / 0x3416: app 0x412fe0 sets app+0x308 = 1 or 2 and calls **GotoNode("race")**.
- r = 0x3417..0x3419 (a race was accepted), by race type:
  - 0 → app+0x310 = 0xD, app+0x314 = 0
  - 2 → app+0x310 = 0xE, app+0x314 = 4
  - 1 → app+0x310 = 0xC, app+0x314 = 3

  Then sends node->ListenToMessage(r, 0). The node handles the race start *(inferred)*.
- Any other r: node->ListenToMessage(r, 0).
- Finally page = 0 and race type = 3.

| addr | name | purpose |
|---|---|---|
| 0x405b60 | Build | creates the anims/buttons above, sets +0x4C = 1 (log text "BarbiePager::Build() - trying to build a pager in a invalid palette") |
| 0x4061f0 / 0x406250 | Hide / Show | hide or show the body, the 4 buttons (0x40f240) and both LCDs. Show positions everything at the viewport origin and resets answered. |
| 0x406940 | HitTest | pager body vt+0x70 |
| 0x406b20 | IsShowing | +0x18 anim visible. The toolbar SpendTime also calls it on the saddlebag object, which has the same +0x18 layout. |
| 0x406b40 | ShowLCD(which) | 0 shows Christie and hides Teresa; 1 the reverse |
| 0x4060c0 | String::operator=(const char*) | COMDAT helper used across the exe |

#### BarbieSaddlebag *(name inferred)* — toolbar node+0xD4, popup kind 4

The saddlebag inventory popup. It holds 6 item slots plus 3 "picture" sub-slots. Clicking an item opens it:
notes, certificates with checkmarks, the locket music box and the bottle message.
- Bases: BarbiePopup at +0 (vtable 0x7f0260: **+0 0x406c40 dtor**, **+4 0x407a80 Open(node)** (→ Show 0x4078b0),
  **+8 0x407a90 → 0x4070f0 Close**, **+0xC 0x4086b0 MouseDown**, **+0x10 0x409ad0 HitTest**) and
  OMediaPeriodical at +4 (vtable 0x7f0258: +0 0x409c50 dtor thunk (−4), **+4 0x409c20 SpendTime**).
- ctor 0x406bb0(node). Dtor 0x4070a0. Scalar deleting dtor 0x406c40.
- Size 0x30.
- Created by 0x41bf40 for button type 4, followed by RestoreFromRecord (0x407aa0), Build (0x406c60) and
  Close (0x4070f0).

| off | type | meaning |
|---|---|---|
| +0x0C | u16 | periodical paused (1 = not blinking) |
| +0x10 | app* | node+0x90 |
| +0x14 | node* | |
| +0x18 | HC2DAnim* | `saddlebag<sfx>` |
| +0x1C | HC2DAnim* | `sbFlap<sfx>`. Clicking it makes HitTest return 0, which closes the bag. |
| +0x20 | u16 | notify flag. AddItem blinks the bag button only when this is set; RestoreFromRecord clears it. |
| +0x22 | s16 | blink count |
| +0x24 | u16 | blink phase |
| +0x28 | DWORD | last blink tick |
| +0x2C | HC2DAnim* | toolbar bag button anim (0x41c4f0(4)) |

**The inventory is global (static), not per object.** It survives node changes.
- `char g_bagSlot[6][0x20]` at 0x809970: the anim name of the item in each slot, including the In/Out suffix.
- `char g_bagPic[3][0x20]` at 0x809a30: picture sub-slots.
- `int g_bagPalette` at 0x809a90: the palette the names were built for.
- `int g_slotAnimId[6]` at 0x7fe958 (writable .data, initial value 0x36B9).

**Item ids.** These are the argument of AddItem / HasItem / RemoveItem and of the toolbar wrappers 0x41d1a0
AddBagItem(item, quiet), 0x41d200 HasBagItem and 0x41d220 RemoveBagItem. The tables are indexed from 0:
names 0x7fe7b8, Out-names 0x7fe820, slot 0x7fe888, anim id 0x7fe8f0.

| item | anim name | slot | anim id |
|---|---|---|---|
| 0 | horseshoe | 0 | 0x36B0 (14000) |
| 1–8 | mission1..mission8 | 1 | 0x36B1 |
| 9–12 | certificate1..certificate4 | 1 | 0x36B1 (adding one clears all 3 picture sub-slots) |
| 13 | locketNote1 | 2 | 0x36B2 |
| 14 | locketNote2 | 2 | 0x36B3 |
| 15 | locket | 2 | 0x36B4 |
| 16,17,18 | "c1 rosePic", "c1 tulipPic", "c1 daisyPic" | 3 (pic sub-slot item−16) | 0x36B5 |
| 19,20,21 | "c2 owlpic", "c2 hawkpic", "c2 bbirdpic" | 3 (pic sub-slot item−19) | 0x36B6 |
| 22 | "c3 rainbowPic" | 3 (pic sub-slot 0) | 0x36B7 |
| 23 | bottle | 4 | 0x36B8 |
| 24 (0x18) | coin | 5 | 0x36B9 |

- Names get the palette suffix from 0x41d150 (node+0x108: 0 "In", 1 "Out", 2 "Frog", 3 "Sign", 4 "Vine").
  Exceptions: the horseshoe uses "horseshoeOut" when the suffix is "Vine", and the coin uses "coinOut" when the
  suffix is "Frog".
- Item 0x18 (coin): AddItem and the toolbar wrapper set **app+0x2AC = 1**; RemoveBagItem sets it to 0.

**AddItem(item, quiet)** 0x407b40:
1. Returns at once if HasItem.
2. Deletes the old anim of the slot, or clears the picture sub-slots for certificates.
3. Writes the name into the slot or sub-slot.
4. Creates the anim at the viewport with its id, stores g_slotAnimId[slot] = id and hides the anim.
5. If notify && !quiet: StartBlink (0x409b20). The toolbar button sequence becomes 3 and blinks 3× at 1 s
   (0x409bb0/0x409c20), then StopBlink 0x409b60.

**HasItem** 0x408310: case-insensitive compare of name+suffix with the slot string; slot 3 also checks the 3
picture sub-slots.

**RemoveItem** 0x4084e0:
- Deletes the anim and clears the slot name.
- *Original bug:* for picture items (slot 3) it deletes a matching picture anim but clears `g_bagSlot[i]`
  (i = 0..2, i.e. horseshoe, mission and locket slots) instead of `g_bagPic[i]`.

**RestoreFromRecord** 0x407aa0. Runs with notify = 0 and quiet adds:

| condition | AddItem |
|---|---|
| **app+0x274** u32 ≠ 0 | 0 (horseshoe) |
| **app+0x258** ≠ 0 | 15 (locket) |
| **app+0x26C** ≠ 0 | 23 (bottle) |
| **app+0x25C** ≠ 0 && **app+0x2AC** (u16) ≠ 0 | 24 (coin) |

Missions, certificates, notes and pictures are re-added by the nodes that grant them; the callers are the TrailNode mission code and the bulletin-board panels (nodes.md).

**Build** 0x406c60:
- Creates the bag and flap anims.
- If g_bagPalette equals the node palette (clamped to 0/1): creates anims for every non-empty slot and picture
  name. Slot ids come from g_slotAnimId. Picture ids are 0x36B5 if the name starts with "c1", otherwise 0x36B6,
  so c3 rainbowPic is re-tagged 0x36B6.
- Otherwise: rewrites each stored name's suffix In↔Out in place (scans for the first 'I'/'O'), deletes and
  re-creates the picture anims, then creates the slot anims.
- Finally sets g_bagPalette = node palette.

**Show/Open** 0x4078b0: if hidden: shows the bag and flap. For every slot: finds or creates the anim, shows it,
sets sequence 0 (closed) and positions it at the viewport. Does the same for the picture sub-slots.

**Close** 0x4070f0:
1. Hides the bag, the flap and all item/picture anims.
2. Deletes the checkmark anims of the certificate in slot 1. Each check anim is "cN checkK"+sfx and exists only
   while its flag is set:
   - certificate1: c1 check1..3 ← **app+0x2AE, +0x2B0, +0x2B2** (u16)
   - certificate2: c2 check1..3 ← **app+0x2B4, +0x2B6, +0x2B8** (u16)
   - certificate4: c4 check1 ← app+0x328 && app+0x32A; check2 ← app+0x32C; check3 ← app+0x322 && app+0x324;
     check4 ← app+0x32E; check5 ← app+0x326 (u16, app fields above the saved record)
   - certificate3 has no checkmarks.
3. Stops sound "musicbox".

**MouseDown** 0x4086b0 (event+0x20 == 1). Returns 0 in every case. ToggleItemAnim 0x408670 flips sequence 0↔1
and returns 1 if the item is now open (2 if there is no anim).
- 14000 horseshoe:
  - Opened: anim+0x84 = 0, "shoeopen", DelayVoice(app+0x29E == 0 ? db04069 : db04071, 800).
  - Closed: anim+0x84 = 1, "shoeclose".
- 0x36B1 mission/certificate (slot 1): first closes the slot-2 item if it is open.
  - Opened: "noteopen", creates the checkmark anims as listed under Close. In the "In" palette, c1/c2 checks get
    an offset of (−1, +3).
  - Closed: "notein", deletes the checkmarks.
- 0x36B2 locketNote1: opened → "noteopen", PlayVoice(db0013), NoFlap(db04003); closed → "noteclose".
- 0x36B3 locketNote2: opened → "noteopen", PlayVoice(db04004); closed → "noteclose".
- 0x36B4 locket:
  - Opened: "locketOpen", plays the "musicbox" loop. If HasItem(23 bottle), plays PlayVoice(db02062).
  - Closed: StopSound musicbox, "locketClose".
- 0x36B5 / 0x36B6 (c1 / c2 pictures): toggles the slot-3 anim, plays noteopen or noteclose, and toggles all 3
  picture anims.
- 0x36B7 (c3 rainbowPic): toggles slot 3 and picture 0.
- 0x36B8 bottle: toggles it and plays PlayVoice(db04021).

**HitTest** 0x409ad0: returns 0 if the flap is hit, otherwise the bag body hit result.

#### BarbieCamera — app+0x184

The photo manager. It stores toolbar-camera snapshots in a per-player OMT database file. The class has no data
members (`new(1)` in the app run function 0x411c70); all of its state is static Strings.
- ctor 0x404080(const char* appDir), where appDir = app+0xDC. No dtor or vtable.
- Statics:
  - `s_photosDir` String at 0x809960 = appDir + `"Photos\\"`. Initializer 0x403f20/0x403f30; atexit dtor 0x403fa0
    (guard bit 1 of 0x809950).
  - `s_photosFile` String at 0x809958 = photosDir + `"photos.omt"`. Initializer 0x403fd0/0x403fe0; atexit dtor
    0x404050 (guard bit 2).
- The ctor calls CreateDirectoryA(photosDir). On failure it logs "BarbieCamera::BarbieCamera() - unable to create
  saved game directory".

**Photo file format:**
- One file per save slot: **`<appDir>Photos\photos.omt<slot>`**. `<slot>` = [0x809ac0] (current save slot)
  formatted with `%ld`. There is no extension after the number, e.g. `photos.omt0`.
- The file is an OMT 0MFS container opened through OMediaFileStream (0x45e280) with open(2, 0, 1)
  (read/write, create *(inferred)*). It is wrapped by the OMT database/formatted-stream class 0x460100
  (vtable 0x7f4de0). The slots used are vt+0x18 SetChunkSize, vt+0x1C OpenChunk(type,id) (creates it if
  missing), vt+0x20 DeleteChunk(type,id), vt+0x30 FindTypeNode(type) and vt+0x34 FindChunk(typeNode,id).
- Each photo is one **`'Canv'` chunk (0x43616E76) with id 1..10**. A player can hold at most 10 photos.
- The chunk body is an OMediaCanvas (class 0x45f6a0, 0x34 bytes) serialized with its streamable write method
  (`stream << canvas`, 0x4600c0 → canvas vt+8). It is read back with 0x4600e0 (vt+4).
- Stored canvases are full-screen **640×480, 8-bit** grabs (created with vt+0x14(640, 480, pixels, 0, 8)).

| addr | name | purpose |
|---|---|---|
| 0x404080 | BarbieCamera(appDir) | builds the two paths and creates the Photos\ directory |
| 0x404200 | TakePhoto(src) → u16 ok | `src` = *(app+0x3C) (screen/world object). Creates a canvas, calls vt+0x10(0x22D) (flags *(inferred)*), copies the palette from the current node's background canvas (app+0xEC → +0x98) through canvas+0x24 → vt+0x70, then allocates 640×480×8 from `src+0x5C → vt+0x28()` pixels. Calls SavePhoto and deletes the canvas. Called by the toolbar camera button 0x41c840, which hides the toolbar around the grab and plays the "camera" sound on success. |
| 0x4042d0 | SavePhoto(canvas*) → u16 | opens `photos.omt<slot>`, id = FindFreePhotoId. If an id is free: OpenChunk('Canv', id), SetChunkSize(0), writes the canvas, returns 1. Returns 0 if all 10 are used. Closes the file (stream vt+0x28) and deletes the stream. |
| 0x4044a0 | FindFreePhotoId(db) → int | the first id in 1..10 with no 'Canv' chunk, or 0 |
| 0x4044e0 | DeletePhoto(int id) | opens the file and calls db vt+0x20('Canv', id). Called by the photo album's trash button (0x415d30), which deletes album index i as id i+1. |
| 0x404660 | LoadPhotoThumbnail(int id) → canvas* | if the file exists (0x45e840) and the chunk exists: reads the canvas, closes the file, scales in place to **220×165**. Returns 0 if missing. Album list (0x416610) uses ids 1..10. |
| 0x4048f0 | LoadPhoto(int id, bool half) → canvas* | same as above. If `half`, scales to **320×240**; otherwise returns the raw 640×480. The album viewer 0x415490 uses (id,1) for display and (id,0) followed by ScaleCanvas(…, 220, 166, 0). |
| 0x404b90 | ScaleCanvas(src, w, h, srcIsHalf) → new canvas | GDI StretchBlt (SetStretchBltMode COLORONCOLOR = 3) of source rect 0,0–(srcIsHalf ? 320×240 : 640×480) into a new w×h canvas with the same palette |
| 0x404cf0 | ScaleCanvasInPlace(canvas**, bool small) | replaces *canvas with a 320×240 copy (small = 0, sizes at 0x7fe3c4/0x7fe3c8) or a 220×165 copy (small = 1, 0x7fe3cc/0x7fe3d0) |
| 0x404e50 | static DeletePhotoFile(int slot) | deletes the file `photos.omt<slot>` if it exists (0x45eaf0). Called by the delete-player code 0x429690 right after it deletes the savegame file. |
| 0x403f20..0x404050 | static String init/atexit (8 functions) | |

### Bulletin-board panels and the handbook

#### BBPanel *(name inferred)* — vtable 0x7f0c18

A popup drawn over the bulletin board. Layout: +0 vtbl, +4 owner node, +8 background HC2DAnim*.
Vtable (7 slots): 0 Unused0(a,b) 0x40bd60 (no-op) · 1 SoundEnded(name,loop) 0x40bd60 (no-op) · 2 sdd 0x40bdf0 ·
3 Hide 0x40bda0 (bg SetHidden(1)) · 4 Show 0x40bdb0 · 5 IsVisible 0x40bdc0 (bg+0x48==0) · 6 MouseDown(ev) 0x40bde0 (→ owner's
toolbar MouseDown 0x41c340). The base dtor (0x40bd70, unreferenced copy; inlined in sdd 0x40bdf0) deletes the bg anim by name.
Subclasses: BBClueBook, BBTeamPhoto, BBMembershipCards, BBCertificates, plus Handbook and HandbookPrintDialog (below).
BBPanel is unrelated to the button classes: 0x401000 (ACButton) is an HCRolloverButton subclass with an ACRollover.

#### BBClueBook *(name inferred)* — vtable 0x7f0bf8, 0x28 bytes

The clue book: 8 pages, one per club mission. Shows a "thanks" stamp on completed missions. "Go" starts the mission shown.
ctor 0x40bb90, dtor 0x40be50, sdd 0x40be30. Slots: 1 SoundEnded 0x40c0a0, 2 sdd, 3 Hide 0x40c660, 4 Show 0x40c6d0, 5 IsVisible
(base), 6 MouseDown 0x40c110, **7 SayPage 0x40bf50** (new).
Fields: +8 bg `bb_clue`; +0xC `clues` (id 0x3479, sequence = page 0..7); +0x10 `clueleftpage` (0x3475); +0x14 `cluerightpage` (0x3474);
+0x18/+0x1C arrow buttons `clue_leftarrow`(0x3475)/`clue_rightarrow`(0x3474); +0x20 `thanks` stamp; +0x24 `gobtn_clues` button (0x3478).

| addr | name | purpose |
|---|---|---|
| 0x40bb90 | ctor | create the anims and buttons above |
| 0x40be30/0x40be50 | sdd/dtor | delete buttons and anims |
| 0x40bf50 | SayPage | voice by page: 0 db04139 · 1 db1501+db0031 · 2 db1502 · 3 db1503 · 4 db04017+db0105+sound thunder2 · 5 db1505+db0131 · 6 db1506+(db0146 or db0150, random) · 7 db04025+db0173 |
| 0x40c0a0 | SoundEnded(name) | if global 0x809ad8 and name=="db9011" → GotoNode("rabbitE") |
| 0x40c110 | MouseDown | 0x3474/0x3475: next/previous page (0..7), sound `page`, stamp visible iff app+0x258+4*page ≠0, SayPage. 0x3479 → SayPage. 0x32CE → db0411. **0x3478 Go** (below). Other ids → toolbar |
| 0x40c660 | Hide | hide everything |
| 0x40c6d0 | Show | show, go to page 0, stamp iff app+0x258≠0; **app+0x2CC++** (first: db04138, db9009, db9008, db04139; else db04139) |

Go button (current page p): remove bag items 13 and 14 if present; set **app+0x310 = p** (current mission) and
**app+0x314 = target location** (p→ 0:4, 1:2, 2:7, 3:4, 4:0, 5:4, 6:2, 7:3; same numbering as trailIndex, 7→6 via 0x8027b0
*(inferred)*); AddBagItem(p+1) (the clue item). Voices: p0 db04001 only if app+0x258==0 · p2 db04096 if app+0x260==0 else db02014 ·
p3 0x264→db04015/db0068 · p4 0x268→db04016/db0101 + thunder2 · p5 0x26C→db04020/db0127 · p6 0x270→db04022/db0147 ·
p7 0x274→db04024 / random db0168/db0165/db0167. **Only pages 0–2** test app+0x254 (u16): if 0, set 0x809ad8=1 and say db9011
(→ rabbitE when it ends). Otherwise → 0x41c760(3).

#### BBCertificates *(name inferred)* — vtable 0x7f0da8, 0x58 bytes

The certificates book: 4 pages (0 nature, 1 wildlife, 2 first aid, 3 trail). Earned certificates show check marks.
"Go" starts the certificate mission. ctor 0x40d090, dtor 0x40d3e0, sdd 0x40d3c0, slot1 SoundEnded 0x40dc40 (as in the clue book),
Hide 0x40daf0, Show 0x40db60, MouseDown 0x40d570; helpers HideChecks(page) 0x40dcb0, ShowChecks(page) 0x40dd80.
Fields: +8 bg `bbcloseup` instance `bb_cert`; +0xC `certificatemissions` (0x3479, seq = page); +0x10 `certificateleftpage` (0x3475);
+0x14 `certificaterightpage` (0x3474); +0x18 `gobtn_certs` button (0x3478, moved by x−80); +0x1C..+0x24 `c1_check1..3In`
(instances `c1check1..3bb`); +0x28..+0x30 `c2_check1..3In`; +0x34..+0x44 `c4_check1..5In`; +0x48 u16 hasC1 = app+0x278≠0;
+0x4A u16 hasC2 = app+0x27C≠0; +0x4C u16 hasC4 = app+0x284≠0; +0x50 `thanks` instance `new_thanks` at x+80 (only if app+0x280≠0:
the first-aid certificate); +0x54 = −1 (unused). Check anims are created only for earned certificates, at z −2500, and are shown
all at once. The dtor never deletes c4_check5 (original leak).
MouseDown: 0x3474/0x3475 page ±1 (0..3) with HideChecks(old)/ShowChecks(new), sound `page`. 0x3479 → page voice
db04007/db0216/db0190/db0239. 0x32CE → db0407. **0x3478 Go**: remove bag items 13,14 (pages 0,1 also remove 16..21), then
p0: app+0x310=8, app+0x314=7, AddBagItem(9), **u16 app+0x2AE/0x2B0/0x2B2 = 0** (nature sub-goals reset), voices db0204+db0202 ·
p1: 9, 7, item 10, **u16 app+0x2B4/0x2B6/0x2B8 = 0** (wildlife sub-goals), db0220+db0217 · p2: 10, 3, item 11, db0194+db0191 ·
p3: u16 app+0x322..0x32E (7 fields) = 0, 11, 7, item 12, db0244+db0240. On every page, if app+0x254==0 the voices are replaced
by DB9011 → rabbitE (via 0x809ad8). Otherwise → 0x41c760(3).
Show: **app+0x2D0++** (first: db0407, db9009, db9008; else db9007); page 0 with its checks.

#### BBTeamPhoto *(name inferred)* — vtable 0x7f0c38, 0x10 bytes

Close-up of the club team photo. ctor 0x40c7f0 (bg = `bbcloseup` instance `bb_teamphoto`; +0xC `teamphoto`), dtor 0x40c890,
sdd 0x40c870, Hide 0x40c910, Show 0x40c930, MouseDown slot = 0x40cfe0 (same body as the base, forwards to the owner).

#### BBMembershipCards *(name inferred)* — vtable 0x7f0d78, 0x20 bytes

The membership card, showing rider rank and player name. ctor 0x40c960, dtor 0x40cf40, sdd 0x40cf20, Hide 0x40cff0,
Show 0x40d020, MouseDown 0x40cfe0 (forward to owner).
Fields: +8 bg `bbcloseup` instance `bb_membership`; +0xC `membershipcards` (sequence = **app+0x2BC rank**: 0 Secret Valley Rider,
1 Moon Rider, 2 Star Rider); +0x18 BBTextStyle*; +0x1C BBNameField* showing the **player name = string at app+0x1B0** (record start).
The name field sits at card center-x, card y+72, card z+10 *(approx.)*. Show voice by rank: 0 db03033, 1 db03031, 2 db03032b.

#### Handbook *(name inferred)* — vtable 0x7f17c0, 0x6C bytes

The "Barbie Riding Club handbook": creed cover, photo album (camera tab), horses page, awards/ribbons page,
page arrows and a print button with yes/no confirmation.

- Base BBPanel. ctor 0x417d40, dtor 0x4180f0, scalar deleting dtor 0x4180d0.
- vtable overrides: +0x04 SoundEnded 0x4192f0, +0x08 dtor 0x4180d0, +0x0C Hide 0x419110, +0x10 Show 0x4191f0,
  +0x18 MouseDown 0x418460 (slots +0x00, +0x14 inherited).
- Assets: anims handbook (bg), creed (0x347C), horses (0x347D, frames 0–3, voices db0429/db0430/db0428/db0427),
  print_bg; buttons leftarrow 0x3475, rightarrow 0x3474, printbtn 0x347B, left/rightcamerabtn 0x3478,
  left/righthorsebtn 0x3479, left/rightribbonbtn 0x347A, print_yes 0x347E, print_no 0x347F; sounds page, print,
  null; voices db0425, db02031, db02032, db0413, db0414, db0417.

| off | type | meaning |
|---|---|---|
| +0x0C | anim | creed |
| +0x10 | node | owning node (bulletinboard); node+0x1D6 u16 set on interaction, node+0x1C0 print dialog, node+0x1D4 |
| +0x14 / +0x18 | ACButton* | left / right arrow |
| +0x1C,+0x20 / +0x24,+0x28 / +0x2C,+0x30 | ACButton* | camera / horse / ribbon tab (left, right) |
| +0x34 / +0x38 / +0x3C | ACButton* | currently shown camera / horse / ribbon tab |
| +0x40 | ACButton* | print button |
| +0x48 | u16 | print-souvenir pending |
| +0x4C | HandbookAwardsPage* | (new 0x3c) |
| +0x50 | anim | horses page |
| +0x54 | HandbookPhotoAlbum* | (new 0x128, args node, app+0x184) |
| +0x58 / +0x5C / +0x60 | anim / ACButton* ×2 | print confirm bg / yes / no |
| +0x68 | u32 | section: 0 photos, 1 horses, 2 awards, 3 creed (cover) |

| addr | name | purpose |
|---|---|---|
| 0x417d40 | Handbook::Handbook | create bg, buttons, tabs (right tabs shown), creed; section 3 |
| 0x4180d0 / 0x4180f0 | dtors | delete album (non-virtual dtor 0x416a60 + delete), awards page, horses, creed, buttons, confirm |
| 0x418270 | HideCreed | |
| 0x418290 | ShowAwardsPage(bool) | lazily create HandbookAwardsPage, vt+4 SetHidden |
| 0x418330 | ShowHorsesPage(bool) | lazily create `horses`; app+0x2DC++ (first time voice db02031) |
| 0x4183c0 | ShowPhotoAlbum(bool) | lazily create HandbookPhotoAlbum, SetHidden |
| 0x418460 | MouseDown | arrows (album/horses/awards paging), tab switching (sound `page`), print flow (photos → HandbookPrintDialog stored at node+0x1C0, node vt+0xDC; souvenirs/horses/awards/creed → confirm then print 0x422850/0x4229d0), help 0x32CE (pause idle timer, voices), creed/horses voices |
| 0x419050 / 0x419090 / 0x4190d0 | SetCameraTabSide / SetHorseTabSide / SetRibbonTabSide(bool right) | swap visible tab button |
| 0x419110 | Hide | hide everything, StopVoice |
| 0x4191f0 | Show | show bg/tabs/creed, section 3; app+0x2D4++ (first open pauses idle timer); voice db0425 |
| 0x4192f0 | SoundEnded | restart idle timer (app+0x19C) |

#### HandbookPrintDialog *(name inferred)* — vtable 0x7f1780, 0x40 bytes

Photo print dialog: choose one of 4 frames, print preview, yes/no, prints photo with frame.

- Base BBPanel. ctor 0x415250 (bg `printing_bg`), dtor 0x415670, scalar deleting dtor 0x415470.
- vtable: +0x08 dtor, +0x0C Hide 0x4157c0, +0x10 Show 0x4158d0, +0x18 MouseDown 0x4159a0 (others inherited).
- Assets: buttons printer 0x7534, frame1btn–frame4btn 30000–30003, print_yes 0x7535, print_no 0x7536; anims
  smallframe (z=410.5, sequence = frame), print_bg, bigframe<1–4>; sounds print, bookopen; voice db0414 (help 0x32CE).

| off | meaning |
|---|---|
| +0x0C..+0x1C | printer, frame1..4 buttons |
| +0x20 | smallframe anim |
| +0x24 | photo canvas; +0x28 OMT canvas element showing thumbnail (100,93,410); +0x30 thumbnail canvas 220×166 |
| +0x2C | bigframe anim (temporary); +0x34/+0x38/+0x3C confirm bg / yes / no |

| addr | name | purpose |
|---|---|---|
| 0x415250 | ctor | |
| 0x415470 / 0x415670 | dtors | |
| 0x415490 | SetPhotoByIndex(i) | load photo i via BarbieCamera (0x4048f0), scale to 220×166 (0x404b90), show |
| 0x4155a0 | SetPhotoCanvas(canvas) | same from a ready canvas (souvenir print) |
| 0x4157c0 / 0x4158d0 | Hide / Show | |
| 0x4159a0 | MouseDown | frame buttons → smallframe sequence 0–3; printer → confirm; yes → print photo in `bigframe<n+1>` (0x422600) + sound print; no → close + bookopen |

#### HandbookPhotoAlbum *(name inferred)* — vtable 0x7f17a0, 0x128 bytes

Album section: optional final-payoff award page, souvenir ("stock") pages unlocked by record flags, then 5
spreads of camera photos (10 slots) with a trash can (delete photo).

- ctor 0x416610 (node, BarbieCamera*), dtor 0x416a60. Uses a std::list<int> (shared node pool globals
  [0x809c28]..0x809c38: 0x416560 insert, 0x416970 pool block alloc, 0x4169c0 ~list).
- Assets: trashbtn (33000), payoff_award (0x80ED, only if app+0x2a4==1), leftframe, rightframe,
  transp_leftframe (0x80E9), transp_rightframe (0x80EA), trash_bg, exitBorderIn, trash_yesbtn 0x80EC,
  trash_nobtn 0x80EB; souvenir anims stock_cottage/coin/shoe/locket/bell/well/flower1–3/hawk/owl/bbird/rainbowpic
  with saying_* captions for 0–5 (tables 0x800460/0x800498), voices table 0x8004d0 (db0435, db04112–db04114,
  db0437, db0436, db0231); voice db02033; sounds trash, select, null.

| off | meaning |
|---|---|
| +0x04 u16 | intro voice played; +0x08 node; +0x0C BarbieCamera*; +0x14 selected photo (1-based, 0 none) |
| +0x18 u16 | album empty (a slot failed to load); +0x1C section (−1 award, 0 souvenirs, 1 photos); +0x20 spread 0–5; +0x24/+0x28 left/right photo index |
| +0x2C/+0x30 | transparent frame hotspot anims; +0x34/+0x38 their rollovers |
| +0x3C/+0x40/+0x44 | souvenir list head / count / current |
| +0x48 + 4·k / +0x7C + 4·k | souvenir anim / caption anim for souvenir k |
| +0xB0 | payoff_award anim; +0xB4/+0xB8 leftframe/rightframe; +0xBC + 4·i photo canvases; +0xE4 + 4·i photo elements (i<10) |
| +0x114 | trash button; +0x118..+0x124 trash confirm bg/border/yes/no |

| addr | name | purpose |
|---|---|---|
| 0x416610 | ctor | trash button, BuildSouvenirList, award, load photos 1–10 (0x404660) alternating left (88,69) / right (328,69) at z=410 |
| 0x416a60 | dtor | |
| 0x415d30 | MouseDown | trash (needs a selected photo) → confirm; yes → delete photo (0x4044e0) + sound trash; frame hotspots select left/right photo or speak souvenir voice; award → sound select + GotoNode("finalpayoff") |
| 0x4162f0 | GetAwardAnim | award anim if section −1 |
| 0x416300 | BuildSouvenirList | flags → souvenir ids (see save file) and create their anims |
| 0x416d00 / 0x416fa0 | NextPage / PrevPage | page state machine; returns 0 at the end |
| 0x417200 | SetHidden(bool) | show first page (award, souvenirs or photos) / hide all |

#### HandbookAwardsPage *(name inferred)* — vtable 0x7f17b0, 0x3C bytes

Certificates and ribbons. List of frame ids of anim `certificates`: mission flags app+0x278..0x290 → frames 0–6;
race counts app+0x24c (A) / app+0x250 (B): min(A,B) × frame 9, (A−B) × frame 8, (B−A) × frame 7. Frames < 4 show
the player name (app+0x1b0) as OMT text (font `BureauGrotesque-FiveThree_18bb`, at 320,270).

| addr | name | purpose |
|---|---|---|
| 0x417490 | ctor | build list, create text element (0x465d10) for the name |
| 0x417900 / 0x417920 | dtors | |
| 0x417a50 | FirstPage | |
| 0x417a80 | GetPrintAnim | `award_prints` anim set to current frame |
| 0x417ae0 | IsCertificate | current frame < 4 |
| 0x417af0 | GetNameText | |
| 0x417b00 | SetHidden(bool) | voice db03061 on certificate pages |
| 0x417b90 / 0x417c50 | NextPage / PrevPage | voices db03060 (first frame ≥4), db03059 (first ribbon ≥7) |
| 0x417cc0 | MouseDown | hotspot 0x80EB: frames ≥7 random db03069/db03070, else table 0x800508 (db03063, db03064, db03062 for 4–6) |

Transitions in the handbook family: payoff award → `finalpayoff`. No save calls (counters app+0x2D4/0x2DC are
only saved with the next save).

### Text and font helpers

#### BBTextStyleBase / BBTextStyle *(names inferred)*

Font holder used by BBMembershipCards. Base ctor 0x40cce0 sets an RGB color at +0xC to (255,255,255). Base dtor 0x40cd20
(unlisted). Slots: 0 sdd (base 0x40cd50, derived 0x40cd70 → dtor 0x40cd90, which deletes +8), 1 GetCanvasFont 0x40cd30 (returns +8),
2 GetFontCanvas 0x40cd40 (returns +4). Both getters are linker-shared with vtbls 0x7f33d4/0x7f33e4.
+4 = 'Canv' resource `BureauGrotesque-FiveThree_18bb` (app FindResource 0x44dd10); +8 = 0x314-byte OMT canvas font (0x469030)
built from it with metrics 37/29 (globals 0x7f14dc/0x7f14d8), char map 0x7f14e0 (glyph = char−0x1F for 0x20..0xFF), and
font+0x30E = 37/3.

#### SignInNameTextStyle / SignInCardTextStyle *(names inferred)*

Small (0x14-byte) objects derived from the game's text-style base (vtable 0x7F0D98: [sdd 0x40cd50,
GetRenderer 0x40cd30 (+8), GetFont 0x40cd40 (+4)]; +0xC colour (r,g,b set to 255)). Ctors are inlined in
SignIn::SignIn: load the 'Canv' font `BureauGrotesque-FiveThree_18` (name style) or `_10` (card style),
create a 0x314-byte OMT text renderer (0x469030) initialised with (font, 37, 29, -1) resp. (font, 20, 17,
-1), a 256-entry colour table 0x7F12D0 resp. 0x7F13D8, renderer+0x30E = 12 resp. 6, +0x310 = 1, +0x10C = 1.
- SignInNameTextStyle: vtable 0x7F33E0 [sdd 0x427fa0, 0x40cd30, 0x40cd40]; dtor 0x427fc0 (deletes the
  renderer, restores base vtable).
- SignInCardTextStyle: vtable 0x7F33D0 [sdd 0x428010, 0x40cd30, 0x40cd40]; dtor 0x428030.

#### BBNameField *(name inferred)* — OMT text element (0x168 bytes)

An OMT string-field element (base ctor 0x465d10, like OMediaStringField) with a maximum length. Overrides: main +0x00 sdd
0x40cef0 (base dtor 0x465f20), +0x94 DrawText 0x40cde0 (temporary draw context 0x469c00, then vt+0xA4) *(inferred)*, +0xA4
0x40ce70 (just calls the base 0x466940), **+0xBC SetMaxLength 0x40ce90** (new; +0x164). The +0xD4 listener slot 0 is 0x40cea0: it
swallows message −0x68 (character key) when the field is enabled and focused (global 0x80ea1c), param+8==0, and length
(+0x120) == max (+0x164); otherwise it calls base 0x466370. Thunks 0x40eba0/0x40ebb0/0x40ebc0/0x40ebd0/0x40ebe0/0x40ebf0
(adjust 0x10/0x1C/0x6C/0xC8/0xD4/0xE4).

#### SignInCard *(name inferred)*

One saved-player card on the sign-in screen: a "smallcard" anim plus an editable name text element.
- ctor 0x426ee0 `(ownerNode, const char* playerName, int index, font*)`. The anim is CreateAnim("smallcard",
  "smcard<index>") with hotspot id index+1000. The text element is `new(0x168)` (base 0x465d10, vtables
  0x7f0ca8/…/0x7f0c60), id index+1000, maximum 40 characters, width 140, font from font->vt+4. Its text is set to
  playerName.
- Scalar deleting dtor 0x427110. Dtor 0x427130 (DeleteAnim by name, delete the text element).
- Vtable: [0x427110, Hide(bool) 0x427170, Show 0x427190].
- Fields: +0x04 card anim, +0x14 owner, +0x18 text element.
| addr | name | purpose |
|---|---|---|
| 0x427170 | Hide(b) | Hide(b) on the text element and the card |
| 0x427190 | Show | un-hides both |
| 0x4271d0 | GetName(String* out) | copies the text (element +0x11C, length +0x120) |
| 0x4272d0 | SetPosition(x,y) | moves the card and centres the text horizontally at card y+36, z = card z+1 |

### Timers and Smacker elements

#### HCTimer *(name inferred)* — ctor 0x42fb10

Periodic message timer: an OMediaPeriodical (base ctor 0x45be00) with an OMediaBroadcaster at +0xC
(0x45c2b0) and an embedded OMT timer (OMediaTimerTemplate layout) at +0x18. When running and the elapsed
time reaches the interval it broadcasts (message, 0) to its listeners and restarts. Size 0x34 (new(0x34)).
ctor 0x42fb10(interval_ms), dtor 0x42fba0, sdd 0x42fb80. Name *(inferred)*.

Vtables: 0x7F3738 [+0x00 sdd 0x42fb80, +0x04 SpendTime 0x42fc70, +0x08 SetInterval 0x42fc30, +0x0C
GetInterval 0x42fc40, +0x10 SetDefaultInterval 0x42fc10, +0x14 GetDefaultInterval 0x42fc20, +0x18
ResetInterval 0x42fc00 (interval = default), +0x1C SetMessage 0x42fc50, +0x20 GetMessage 0x42fc60, +0x24
Restart 0x42fd30]; +0x0C 0x7F3760 [thunk sdd 0x42fd90, Broadcast 0x45c580, AddListener 0x45c510,
RemoveListener 0x45c470].

Fields: +0x18 start tick (GetTickCount), +0x1C frozen elapsed, +0x20 initial offset, +0x24 u16 stopped
(ctor: 1 = stopped until Restart), +0x26 u16 locked, +0x28 interval ms, +0x2C default interval ms, +0x30
message id. Elapsed = stopped/locked ? +0x1C : now - +0x18 + +0x20.

Uses: the app (0x411c70) creates it with app+0x1A4 = 20000 ms, stores it at app+0x19C, message 50000,
listener = app (app+0x20). App ListenToMessage 0x413020 calls Restart on input messages -0x67/-0x66 and
forwards 50000 to the current node's listener ⇒ **inactivity timer: 20 s without mouse input ⇒ message
50000** (nodes play idle hints/thunder and may call SetInterval, e.g. 450000 or (rand%5+1)*2000). Logo
nodes create their own (BarbieSoftLogo 0x40a080 3000 ms, HCLogoNode 0x4194f0 8000 ms, MattelLogo
0x420f60 3000 ms) to advance.

#### HCSmackerElement *(name inferred)*

A 2D OMT element that decodes a full-screen SMK into a **shared 640×480×8 offscreen world**. The world is a static
singleton at 0x809f50 (a DirectDraw surface world 0x473be0, or a GDI world 0x46ee20 when DirectDraw is not in use).
It is wrapped by a shared canvas at 0x809f54 and reference-counted by 0x809f58. Each element plays one SMK through
the Smacker DLL (Open/DoFrame/NextFrame/Goto), with optional palette upload and looping.
Users: opendoor (`new(0x114)`, 12 fps), the node at 0x42f590 (10 fps), and HCSmackerOverlayElement.
- Base classes: OMT canvas element 0x46d800 at +0, and the periodical 0x45be00 at +0xD0.
- ctor 0x41a300 `(fps, applyPalette)`. Size **0x114**. Scalar deleting dtor 0x41a2e0. Dtor 0x41a500 (calls
  SmackSummary into a scratch buffer, then SmackClose; frees the shared world and canvas when the refcount
  reaches 0).
- Vtables: main 0x7f1918 (42 slots). Overrides: +0x00 0x41a2e0, +0x20 Render 0x41a6e0, +0xA0 AdvanceFrame
  0x41a7e0 (new virtual), +0xA4 Open 0x41a700 (new virtual).
  Secondary vtables: +0x10 0x7f1910 [0x41a9f0, 0x45da80]; +0x1C 0x7f1900 [0x41aa00, 0x467230];
  +0x6C 0x7f18f8 [0x41aa10, 0x466ff0]; +0xD0 0x7f18f0 [0x41aa20, SpendTime 0x41a640].

| off | type | meaning |
|---|---|---|
| +0x7C | u16 | element "dirty" flag, set after each decoded frame |
| +0xDC | Smack* | open movie |
| +0xE0 | u16 | DirectDraw display (display vt+0x3C). 1 → the palette goes to the screen after a vblank wait (0x44e910); 0 → world vt+0x70 |
| +0xE4 | char* | file path |
| +0xE8 | u16 | opened/playing |
| +0xEA | u16 | paused (ctor sets 1) |
| +0xEC | u16 | loop (ctor sets 1) |
| +0xEE | u16 | reached the last frame (the "done" flag nodes poll) |
| +0xF0 / +0xF4 | u32 | current fps / maximum fps (both = ctor fps) |
| +0xF8 / +0xFA | u16 | auto-accelerate (fps+1 per frame, up to 24) / auto-decelerate (fps−1, down to 2) |
| +0xFC | u16 | apply the SMK palette when it changes |
| +0x100 | u8* | world pixel pointer (from world vt+0x64), passed to SmackToBuffer |
| +0x104/+0x106 | s16 | world width/height |
| +0x108/+0x10A/+0x10C/+0x10E | s16 | source rect x, y, w=640, h=480 (used by the 2× overlay blit) |
| +0x110 | u16 | centre the movie (used by the overlay class) |

| addr | name | purpose |
|---|---|---|
| 0x41a300 | ctor | Builds or shares the world and canvas, sets the defaults above, refcount++ |
| 0x41a500 | dtor | see above |
| 0x41a610 | GotoFrame(n) | If n < Frames: SmackGoto(n), clear done |
| 0x41a640 | SpendTime | If playing, not paused, at least 1000/fps ms since the global last-frame time 0x809f5c, and SmackWait==0: AdvanceFrame, then apply the auto-accelerate/decelerate rules |
| 0x41a6e0 | Render | Calls the base render (0x468ef0) only while playing and not paused |
| 0x41a700 | Open(path, preload) | SmackOpen with flags 0xFE000, plus 0x200 if preload, plus 0x80 with SmackFrameRate(fps) if fps≠0. Locks the world, calls SmackToBuffer(world ptr, w, h), unlocks |
| 0x41a7e0 | AdvanceFrame | If looping or not done: when NewPalette and +0xFC, expand the 256 RGB entries into a palette object (0x476090) and send it to the world or screen. Lock, SmackDoFrame, unlock, mark dirty. At the last frame: SmackGoto(1) if looping, else done=1. Otherwise SmackNextFrame |
| 0x41a990 | SlowDown | fps−1, minimum 2 (called by trail nodes 0x435980/0x436f10) |
| 0x41a9c0 | SpeedUp | fps+1, maximum +0xF4 |
| 0x41a9f0..0x41aa20 | adjustor thunks to 0x41a2e0 | |

#### HCSmackerOverlayList *(name inferred)*

Abstract base: [pure, pure, RewindOverlays 0x41ab20, pure]. It owns a singly linked list of overlay SMKs.
- ctor 0x41aa30, dtor 0x41aa80 (SmackClose and radfree for owned nodes; the last instance closes the shared
  SmackBlit handle 0x809f60, refcount 0x809f64).
- Fields: +0x04 radmalloc'd decode buffer for the main movie; +0x08 list head (0x1C-byte nodes:
  +0 Smack*, +4 buffer, +8 u16 enabled, +0xC next, +0x10 u16 owned, +0x14 x, +0x18 y); +0x0C u8 transparent
  index; +0x0D..+0x0F key RGB (default 255,255,255); +0x10/+0x11 u8 palette scan range (0..255); +0x12 s16
  horizontal offset for overlays.
- 0x41ab20 RewindOverlays: SmackGoto(node,1) for every node.

#### HCSmackerOverlayElement *(name inferred)*

- Bases: HCSmackerOverlayList at +0 and HCSmackerElement at +0x14. ctor 0x41ab50 `(fps, double2x)`. Size
  **0x130** (`new(0x130)` in 0x438930; also created in 0x437e50 and 0x439160). Opens the shared SmackBlit with
  SmackBlitOpen(double ? 3 : 1).
- Vtables: +0x00 0x7f1aa0 [AdvanceFrame 0x41afe0, AddOverlay 0x41ae20, Rewind 0x41afb0, SpendTime 0x41acb0];
  +0x14 0x7f19f8 (copy of 0x7f1918 with +0x00 0x41ac30, +0xA0 0x41b630 thunk to 0x41afe0, +0xA4 Open 0x41adb0);
  +0x24 0x7f19f0 [0x41b640, …]; +0x30 0x7f19e0 [0x41b650, …]; +0x80 0x7f19d8 [0x41b660, …];
  +0xE4 0x7f19d0 [0x41b670, 0x41b680 = SpendTime thunk].
- Own fields: +0x128 u16 double-size (2×) blit; +0x12A u16 palette apply pending; +0x12C palette object.

| addr | name | purpose |
|---|---|---|
| 0x41ab50 | ctor | |
| 0x41ac30 | scalar deleting dtor (entered on the +0x14 base) | |
| 0x41ac60 | dtor | HCSmackerElement dtor, then the list dtor |
| 0x41acb0 | SpendTime | fps gate (global 0x809f68); SmackWait on every enabled overlay and on the main movie; when all are ready → AdvanceFrame; then the auto speed rules |
| 0x41adb0 | Open | Base Open. In 2× mode it also radmallocs a W×H buffer at +0x04 and calls SmackToBuffer into it |
| 0x41ae20 | AddOverlay(path) | SmackOpen(path, 0xFE200, or 0xFE280 if fps≠0). Appends a node with its own radmalloc buffer (owned=1, enabled=0) |
| 0x41af10 | AddSharedOverlay(node*) | Appends a copy of an existing node (not owned). No callers found |
| 0x41afb0 | Rewind | Paused=1, GotoFrame(1), clear done, RewindOverlays |
| 0x41afe0 | AdvanceFrame | Palette: scans palette indices +0x10..+0x11 for the key RGB to find the transparent index, builds the palette and sends it to the world or screen. DoFrame for the main movie and each enabled overlay. Blits the main buffer (1×, or 2× with SmackBlitSetFlags 2 into the 640-pitch world at (x/2, y/2)), then SmackBlitTrans for every overlay (640-wide overlays scroll by +0x12; others are clipped at their x,y). Advances or loops each movie. Applies a pending palette |
| 0x41b5e0 | ApplyPalette | Sends +0x12C to the screen (DirectDraw) or to the world |
| 0x41b630..0x41b680 | thunks | |

### Desktop horse, printing, logging, save helpers

#### DesktopHorse (free functions) and ComInitializer

Installs/removes the "Desktop Horse" companion program on quit. All three log to log.txt via Log::Write
(level 3, file E:\Projects\Omt\Godiva\DesktopHorse.cpp). Globals: [0x7ff7b4] = installed coat (initial 4),
[0x7ff7b8] = selected coat (initial 4), [0x7ff7bc] = "create DesktopHorse dir" (initial 1).
Coat dirs (table 0x7f1148): 0 `Palamino\`, 1 `White\`, 2 `Paint\`, 3 `Dapple Grey\`; 4 = none.
Bitmaps (table 0x7f1158, 17): dark, feed, General1–5, Main, Start, Stop, Subtle1–4, Tail, Walk, Wash (.bmp).

| addr | name | purpose |
|---|---|---|
| 0x410fe0 | DesktopHorse::CreateShortcut | ShellLink (stack object, ctor 0x426c90): target `<installdir>DesktopHorse\Horse.exe`, link `<CSIDL_DESKTOPDIRECTORY>\Desktop Horse.lnk`, working dir `<installdir>DesktopHorse\`, description "Desktop Horse Program", create (0x426db0) |
| 0x411250 | DesktopHorse::DeleteShortcut | same link path, DeleteFile (0x426de0) |
| 0x411430 | DesktopHorse::CopyCoatBitmaps | if selected==4 or selected==installed → log "No change needed"; else for each bmp: copy `<cddir>DesktopHorse\<coat>\<bmp>` → `<installdir>DesktopHorse\<bmp>` (CreateDirectory once, SetFileAttributes NORMAL, CopyFile overwrite; logs "Copying %s to %s", "CopyFile failed.") |
| 0x4117f0 | DesktopHorse::SetInstalledCoat | [0x7ff7b4] = coat (LoadGame 0x428080 passes record+0x10) |
| 0x411800 | DesktopHorse::SetSelectedCoat | [0x7ff7b8] = coat (stable 0x42ad00 coat buttons 0x3840–0x3843 → 3,1,2,0) |
| 0x4111f0 | ComInitializer::`scalar deleting destructor'` | vtable 0x7f119c (1 slot); CoUninitialize when refcount [0x80a140] hits 0 |
| 0x411230 | ComInitializer::~ComInitializer | same without delete |

Caller: HCToolbarNode::QuitGame 0x41e630: CopyCoatBitmaps; app+0x348 ? CreateShortcut : DeleteShortcut;
Godiva::QuitGame(app+0x34a). ShellLink (derived from ComInitializer, vtable 0x7f33b0): +4 link path,
+0x108 working dir, +0x20C target, +0x310 description.

#### DesktopShortcut *(name inferred)*

A COM IShellLink wrapper used to create or delete "Desktop Horse.lnk" (callers 0x410fe0 create, 0x411250 delete).
The ctor 0x426c90 calls CoInitialize on the first instance (count 0x80a140). Scalar deleting dtor 0x426cc0.
Fields: +0x004 link path (desktop folder + name), +0x108 target path, +0x20C working directory,
+0x310 description (0x100). Size ≥ 0x410 (stack object *(inferred)*).
| addr | name | purpose |
|---|---|---|
| 0x426ce0 | SetTargetPath | strncpy to +0x108 |
| 0x426d00 | SetWorkingDirectory | strncpy to +0x20C |
| 0x426d20 | SetDesktopLinkName(name) | SHGetSpecialFolderLocation(CSIDL_DESKTOPDIRECTORY 0x10) + name → +0x004 |
| 0x426d90 | SetDescription | strncpy to +0x310 |
| 0x426db0 | Create | CreateShellLink(+0x108, +0x004, +0x310, +0x20C) == S_OK |
| 0x426de0 | Delete | DeleteFileA(+0x004) |
| 0x426e00 | CreateShellLink | CoCreateInstance(IShellLink): SetPath, SetDescription, SetWorkingDirectory, then IPersistFile::Save(wide link path) |

#### Print helpers (free functions)

Each renders into a 32-bpp canvas, converts it to a DIB (0x44ee20) and prints it with 0x44e930 (document
"Barbie Riding Club", header "© 1998 Mattel, Inc.  All Rights Reserved", footer = the same text + 11 spaces + the
player name). The player name is **app(0x80a384)+0x1B0** (record +0x00), padded with spaces to 68 characters.
| addr | name | purpose |
|---|---|---|
| 0x422600 | PrintCanvasCentered(fg, bg) | bg copied, fg centred on it (caller 0x4159a0) |
| 0x422850 | PrintElement(elem) | prints elem's canvas (vt+0x7C) at its bounds (caller 0x418460) |
| 0x4229d0 | PrintElementOnWhite(elem) | draws elem into a white-filled GDI world first (caller 0x418460) |

#### Log (free functions)

| addr | name | purpose |
|---|---|---|
| 0x419350 | thunk_RotateLogFile | jmp 0x419360 (not in Ghidra's function list) |
| 0x419360 | RotateLogFile | remove `log_old.txt`, rename `log.txt` → `log_old.txt` |
| 0x419380 | Log::Write(level, srcFile, line, build, logFile, fmt, ...) | vsnprintf (256); level 1 "CRITICAL ERROR", 2 "ERROR %s Line %5d Build…", 3 "Message: %s Line %5d: %s", 4 plain; OutputDebugString and append to logFile |

#### Save-game helpers (free functions)

- Static Strings (initializer / atexit pairs; guard byte 0x809a98):

  | String | address | value | functions |
  |---|---|---|---|
  | s_savedGamesDirName | 0x809ab8 | "Saved Games\\" | 0x409c60/0x409c70, 0x409cd0/0x409ce0 |
  | s_saveBaseName | 0x809ab0 | "savegame" | 0x409d10/0x409d20, 0x409d80/0x409d90 |
  | s_saveExt | 0x809aa0 | ".brc" | 0x409dc0/0x409dd0, 0x409e30/0x409e40 |
  | s_saveDir | 0x809aa8 | "" at start; the app run function 0x411c70 sets it to appDir(app+0xDC) + "Saved Games\\" and calls CreateDirectoryA | 0x409e70/0x409e80, 0x409ee0/0x409ef0 |

- **SaveGame(record*, slot)** 0x401df0:
  1. path = s_saveDir + "savegame" + `%ld`(slot) + ".brc", e.g. `Saved Games\savegame0.brc`.
  2. Creates an OMediaFileStream (`new(0x10)` 0x45ef30, init vt+8) and sets **[0x809ac0] = slot**.
  3. Opens the file with open(0, 0, 1) through 0x45e5a0 (write/create *(inferred)*).
  4. Writes **exactly 0x150 bytes** from `record` (0x45e9b0), closes the file (0x45e720) and destroys the stream.
  5. Callers pass app+0x1B0.
- **CreateNewSaveGame(String* name, int slot)** 0x409f20: builds a fresh 0x150-byte record on the stack and
  calls SaveGame. Called by sign-in 0x428a00. The default layout is in [savegame.md](../formats/savegame.md).

## Shared code worth knowing when porting

- **Identical-code folding.** The linker merged identical functions, so one address can appear in several
  unrelated vtables: `0x40bd60` (`ret 8`) fills HCNode hook slots +0xAC/+0xB0 in every node and BBPanel slots 0/1;
  `0x401b80`/`0x401b90`/`0x401ba0` are the other empty hooks; `0x40a250`, `0x421130`, `0x421190`, `0x4211b0`,
  `0x419ed0` are shared by the three logo nodes; `0x43cd40` is both VineGame's and Stable's SoundEnded; `0x4140e0`
  is the KeyDown forwarder of Grooming and BellGame; `0x403f00` is the +0x6C adjustor of every node. Treat these
  as one function per role, not as class methods.
- **Adjustor thunks.** Every node has four `sub ecx, N; jmp sdd` thunks (N = 0xC, 0x18, 0x6C, 0x7C) used as slot 0
  of its secondary vtables; methods reached through the +0xC (SpendTime) and +0x18 (ListenToMessage) tables run
  with `this` = node+0xC / node+0x18. All offsets in these docs are already converted to node offsets.
- **Static strings.** Several files define global `String` objects with dynamic initializers and atexit
  destructors (save paths, photo paths, preference paths); see the save helpers and BarbieCamera.
- **STL.** HP-STL `list`/`vector` instantiations (`std::list<HCAnimFrame*>`, `std::list<int>`) with pooled node
  allocators appear in the button and logo files; they are plain containers.

## Open questions

- Real names of the classes marked *(name inferred)*; no RTTI is present except `OMediaError`.
- HCNode slots +0x9C..+0xB8 are never overridden or called by game code; their intended role is unknown.
- HCToolbarNode +0xC6/+0xC8/+0xCA/+0xCC edge-cursor modes are only partly decoded (input.md §8 has the cursor
  rules); key code 1 is assumed to be Escape.
- BBPanel slot 0 (two arguments, always a no-op) has no known caller.
- The 5-entry race-history table at rec+0x38 is read by the race but never written after a new game in this
  executable (see savegame.md), so race difficulty never promotes *(inferred from a full scan for writers)*.
