# Nodes (screens) of the game layer

A *node* is one screen of the game: an `HCNode`-derived object created by name in `Godiva::CreateNewNode`
(`0x412140`) and destroyed when the application switches to the next node. This document describes every node
class: purpose, assets, fields, methods, and the transitions it can make. The node framework (vtable slots,
`HCToolbarNode` toolbar, callbacks) is in [classes.md](classes.md); the player record (app+0x1B0) in
[../formats/savegame.md](../formats/savegame.md).

Conventions: `node+X` = offset in the node object; `app` = the Godiva application (node+0x1B8); `rec+X` =
app+0x1B0+X (saved). "GotoNode(n)" = `app->vt+0x44(n, 1)` (`0x413370`), which queues the switch and lets the
application fade out, delete the node and create the next one (see [application.md](application.md));
"RequestNode(n, steps, flag)" = the engine call `app->vt+0x1C` (`0x44de80`) it wraps. Voices are `PlayVoice`
(`0x41d270`, with mouth flaps) unless noted; `NoFlap` = `0x41d330`. Click ids are the clicked anim's +0x11C
(see [animation.md](animation.md)); 0x32CE is the toolbar Help button in every toolbar node. Messages: 13000 =
screen-edge click while the node's edge flags are set, 50000 = 20 s inactivity (idle timer app+0x19C),
−0x66/−0x67 = raw mouse down/up, −600/−601 = world mouse down/up (see [input.md](input.md)).

Helper shorthands used below: `SetHidden(b)` = anim vt+0x30(b) (a hide counter, anim+0x48); `SetSeq(a,n)` =
`(a+0xC4)->vt+4(n,1)` (play sequence n; anim+0xD8 = current sequence); `ForceHideAnim`/`ForceShowAnim`
(`0x43baa0`/`0x43bb30`, copies at `0x401520`/`0x401580`) call vt+0x30/vt+0x38 until the anim is hidden and paused /
visible and running; `HasBagItem` = `0x41d200`; `PlayVoicePaused` = `0x41d310` ("pause" then the voice); the
"cursor-hotspot helper" is an `ACRollover` (`0x401160`, hand cursor while the mouse is over one anim).

## Overview

| factory name | class | ctor | size | base | role | exits (normal flow) |
|---|---|---|---|---|---|---|
| MattelLogoNode | MattelLogoNode | 0x420f60 | 0xD0 | HCNode | Mattel splash, 3 s or click | HCLogoNode |
| HCLogoNode | HCLogoNode | 0x4194f0 | 0xD8 | HCNode | Human Code logo with water ripple, 8 s / end of "music" script / click | BarbieSoftLogoNode |
| BarbieSoftLogoNode | BarbieSoftLogoNode | 0x40a080 | 0xD0 | HCNode | Barbie Software splash, 3 s or click | preferences |
| preferences | Preferences | 0x4216d0 | 0x1FC | toolbar | options (music, controls, desktop horse) | signin |
| signin | SignIn | 0x427370 | 0x25C | toolbar | choose / create / delete a player | intromovie |
| intromovie | IntroMovie | 0x41e990 | 0x1C8 | toolbar | intro movie and performance benchmark | yard (no horse yet) / tackroom |
| yard | Yard | 0x43cda0 | 0x214 | toolbar | barn yard: first-time intro, mount, trail signs | stable / tackroom (left edge), rabbitE, beachE |
| stable | Stable | 0x429d90 | 0x25C | toolbar | choose and name the horse | tackroom |
| tackroom | TackRoom | 0x42c0b0 | 0x2A0 | toolbar | tack up, feed, tack-room tour | grooming, bulletinboard, stable, yard |
| grooming | Grooming | 0x413940 | 0x258 | toolbar | comb and brush the horse | tackroom |
| bulletinboard | BulletinBoard | 0x40def0 | 0x1F8 | toolbar | handbook, clue book (missions), certificates, team photo, membership card | tackroom; trail via map |
| appleW … wishingL | TrailNode subclasses | 0x40a2f0… | 0x7FC | toolbar | first-person trail ride, stops, missions | other trails in-node; yard, mini-games, finalpayoff, tackroom |
| applesort | AppleSort | 0x4015e0 | 0x270 | toolbar | apple-sorting pachinko (mission 5) | appleW |
| leapfrog | Leapfrog | 0x41f1f0 | 0x270 | toolbar | frog peg-solitaire (mission 2) | bridgeS |
| bellgame | BellGame | 0x40a980 | 0x25C | toolbar | bell puzzle (mission 7) | bridgeS |
| vinegame | VineGame | 0x43c0c0 | 0x28C | toolbar | pull vines to find the horseshoe (mission 8) | rainbowS |
| carousel | Carousel | 0x40f860 | 0x224 | toolbar | cottage interior, music box (mission 4) | cottageL |
| opendoor | OpenDoor | 0x421200 | 0x1C4 | toolbar | stable-door movie (no caller found) | carousel |
| race | Race | 0x422d00 | 0x284 | toolbar | horse race against Christie or Teresa | rabbitE / beachE |
| finalpayoff | FinalPayoff | 0x411810 | 0x1C4 | toolbar | ending movie | tackroom |
| credits | Credits | 0x410d20 | 0x1C8 | toolbar | credits roll | quit |
| *(unknown name)* | Yard | 0x43cda0 | 0x214 | | factory fallback ("no match") | |

The command line `/node <name>` (or `-node`) replaces the first node (see [application.md](application.md)).
Entries 2 "intro" and 25–31 (EXIT, BOOK, CAMERA, MAP, BAG, HELP, PRINT) of the node-name table `0x7f1f50` have no
factory entry. "opendoor" is created only by name; no code requests it (the carousel plays `OpenDoor.smk`
itself).

## Game flow

```
MattelLogoNode ─3s─▶ HCLogoNode ─8s─▶ BarbieSoftLogoNode ─3s─▶ preferences ─OK─▶ signin ─Go─▶ intromovie
intromovie ─▶ yard (new player: Barbie intro, left edge ─▶ stable)      or ─▶ tackroom (horse already chosen)
stable ─named horse, OK─▶ tackroom ◀─▶ grooming (brush/comb)   tackroom ◀─▶ bulletinboard   tackroom ─right─▶ yard
yard ─sign─▶ rabbitE / beachE (trail ride; first ride = tutorial on rabbitE)
trail nodes ◀─map / forks / decisions─▶ each other (in-node); appleW end ─▶ yard; map "stable" ─▶ tackroom
mission stops ─▶ applesort (appleW), leapfrog (bridgeS frog), bellgame (bridgeS bell), vinegame (rainbowS),
                 carousel (cottageL) — each returns to its trail with app+0x33C=1 (resume at the stop)
pager chat ─▶ race ─▶ rabbitE / beachE        all 8 missions done ─▶ windmill at cottageL ─▶ finalpayoff ─▶ tackroom
exit dialog "Credits" ─▶ credits ─▶ quit        exit dialog "Yes" ─▶ QuitGame (save if requested, desktop horse)
```

Missions (app+0x310 = active mission, app+0x314 = trail where it happens; neither is saved): eight clue
missions M1–M8 handed out by the clue book, four certificate missions C1–C4 from the certificates book, three
rescue badges R1–R3 offered by the pager, the first-ride tutorial (0x10) and the finale (15). Their step-by-step
logic is in the TrailNode section below; their progress counters are record fields (savegame.md).

## MattelLogoNode — factory "MattelLogoNode"

The first boot splash. It has no visuals of its own; its picture is the `mattellogonode.omt` Canv "mattellogonode
background" that the HCNode base loads *(inferred)*. Base: **HCNode**.
- ctor 0x420f60 `(app, name, next="HCLogoNode", 15000 unused)`. Size **0xD0**. Scalar deleting dtor 0x421080.
  Dtor 0x4210a0. Thunks 0x4211c0/1d0/1e0/1f0.
- Vtables: main 0x7f1e88. Overrides: +0x00 0x421080, +0x0C Tick 0x4211b0, +0x48 Start 0x421130,
  +0x60/+0x68/+0x6C 0x419ed0 (SkipOnInput). +0x0C 0x7f1e80 [0x4211c0, 0x456f30, 0x421080];
  +0x18 0x7f1e70 [ListenToMessage **0x40a250** (BarbieSoftLogoNode's identical handler), 0x4211d0];
  +0x6C 0x7f1e68; +0x7C 0x7f1e58.
- Fields: +0xC4 char* next node, +0xC8 u16 length, +0xCC timer (**3000 ms**, message 0x32E6, listener this+0x18).

| addr | name | purpose |
|---|---|---|
| 0x420f60 | ctor | stores the next node; timer 3000 ms |
| 0x4210a0 | dtor | deletes the timer, frees the name, HCNode dtor |
| 0x421130 | Start | restarts the timer, HCNode::Start |
| 0x421190 | GotoNextNode | `app(+0x90)->RequestNode(next(+0xC4), 0x10, 1)`. Also called by HCLogoNode and BarbieSoftLogoNode (0x40a250) |
| 0x4211b0 | Tick | jumps to HCNode 0x45cf60 |

Transitions: timer 0x32E6 (3 s), a mouse button release or a key → "HCLogoNode".

## HCLogoNode — factory "HCLogoNode"

Shows the Human Code logo. The bottom half of the logo ripples like a water reflection while the "music" script
plays, then the node moves to the next node. **Base: HCNode directly** (ctor 0x454320), not HCToolbarNode.
- ctor 0x4194f0 `(app, name, nextNode, unusedTimeout)` (ret 0x10); the factory passes nextNode="BarbieSoftLogoNode".
  Size **0xD8**.
- Scalar deleting dtor 0x419a90. Dtor 0x419ab0. Adjustor thunks 0x41a2a0/2b0/2c0/2d0 (this−0xC/−0x18/−0x6C/−0x7C).
- Vtables: main 0x7f1828 (50 HCNode slots). Overrides: +0x00 0x419a90, +0x0C 0x4211b0 (Tick: a plain jump to
  HCNode 0x45cf60, shared COMDAT), +0x48 Start 0x419e10, +0x60 AppLeftUp, +0x68 AppRightUp and +0x6C KeyDown all
  0x419ed0, +0x90 ScriptEnded 0x419ea0.
  Secondary vtables: +0x0C 0x7f1820 [0x41a2a0, 0x456f30]; +0x18 0x7f1810 [ListenToMessage 0x419ef0, 0x41a2b0,
  0x45bd80]; +0x6C 0x7f1808 [0x41a2c0, 0x403f00]; +0x7C 0x7f17f8 [0x41a2d0, 0x45c580, 0x45c510, 0x45c470].
- Assets (`hclogonode.omt`): Canv "hclogo", Canv "hclogonode background", SCR "music", WAVE "mystery1" and
  "bumper" (presumably played by the "music" script), and the "splash" HCAn/SMAK.

| off | type | meaning |
|---|---|---|
| +0xC4 | char* | next node name (heap copy) |
| +0xC8 | u16 | length of the next node name |
| +0xCC | element* | static canvas element (0xD0 bytes, ctor 0x46d860) holding "hclogo" |
| +0xD0 | anim* | ripple animation (0xF4 bytes, OMT animated element, ctor 0x4717d0) |
| +0xD4 | timer* | 8000 ms timer (0x34 bytes, ctor 0x42fb10); sends message 0x32E6 to the listener at this+0x18 |

Ctor behaviour:
1. Loads Canv "hclogo" through app 0x44dd10('Canv', name). If that fails, it logs "HCLogoNode: unable to load
   canvas…" and immediately calls `app->RequestNode(next, 0x10, 1)`.
2. Creates the canvas element, attaches it (vt+0x58 with this as the node) and places it centred at
   ((640−w)/2, (480−h)/2, z=250).
3. Builds the ripple animation with 12 frames (k = 1..12). Each frame is a new canvas that is a copy of "hclogo".
   Rows y from h/2 to h−1 are then replaced by source row y+d, where
   d = trunc( sin( ((h−y)·⌊h/14⌋)/(y+1) + 2πk/12 ) · (y+28) · ⌊h/14⌋ / h ).
   A row keeps its original pixels when y+d < 0 or y+d ≥ h. Each frame's value is set to 15.0 through frame
   vt+0x10 (frame duration or rate *(inferred)*).
4. Places the animation at the same centre with z=200, looping (anim+0xC4 vt+0x18(1), +0xE6=1) and hidden
   (Hide(1), vt+0x30).
5. Runs script "music" (0x455890) and starts the 8000 ms timer.

| addr | name | purpose |
|---|---|---|
| 0x4194f0 | HCLogoNode::HCLogoNode | see above |
| 0x419a90 | `scalar deleting destructor'` | |
| 0x419ab0 | ~HCLogoNode | Deletes the canvases owned by the ripple frames (sequence 0), stops and releases all sounds (app 0x44c780(1)), deletes the timer, frees the name, then calls the HCNode dtor |
| 0x419c10 | std::list<HCFrame*>::_Buynode | STL list-node pool allocator (globals 0x809adc–0x809aec) |
| 0x419c90 | std::vector<std::list<>>::_Insert_n | grows the animation's vector of sequence lists |
| 0x419f50 / 0x41a0b0 / 0x41a1e0 | uninitialized_copy / construct / destroy for list<HCFrame*> | STL helpers |
| 0x419e10 | Start | Hides the static element, shows the ripple, sets script var music.faded=1.0, restarts the timer, then calls HCNode::Start |
| 0x419ea0 | ScriptEnded | When the "music" script ends → GotoNextNode (0x421190); then the base handler |
| 0x419ed0 | SkipOnInput | Shared with MattelLogoNode. Mouse up (left or right) or a key → GotoNextNode, then the base no-op |
| 0x419ef0 | ListenToMessage | Message 0x32E6 (timer) → pause the timer and GotoNextNode; otherwise HCNode 0x454b50 |
| 0x41a2a0..0x41a2d0 | adjustor thunks | |

Transitions: the timer (8 s), the end of the "music" script, a mouse button release or a key press each call
`GotoNextNode` → `app->RequestNode("BarbieSoftLogoNode", fade=0x10, 1)`. The request goes through HCApplication
vt+0x1C (0x44de80). That function stores the name at app+0xF0, the fade length (clamped to 16) at app+0xFC, the
flag at app+0x102, and sets the pending flag app+0xF8=1. No save-game calls.

## BarbieSoftLogoNode — factory "BarbieSoftLogoNode"

A timed splash node that shows for 3 s and then goes to the next node ("preferences"). A click or key press skips it.
**Base: HCNode directly** (ctor 0x454320, not a toolbar node). Its layout and behaviour are identical to the
MattelLogo/HCLogo node (ctor 0x420f60, vtbl 0x7f1e88), and it shares that class's out-of-line methods. Both are probably
built from the same inline "timed logo" base, with the copies folded by the linker *(inferred)*.
- Size 0xD0 (factory `new(0xd0)`); factory call `ctor(app, "BarbieSoftLogoNode", "preferences", 15000)` — the 4th arg (15000) is ignored.
- ctor 0x40a080, dtor 0x40a1c0, scalar-deleting dtor 0x40a1a0.
- Vtables: main 0x7f02a8 (50 slots): +0x00 sdd 0x40a1a0; +0x0C Tick → 0x4211b0 (jmp HCNode Tick 0x45cf60); +0x48 Start →
  0x421130; +0x60 AppLeftUp, +0x68 AppRightUp, +0x6C KeyDown → 0x419ed0 (skip: GoNext then base). +0x0C vtbl 0x7f02a0 [thunk
  0x40a2b0, 0x456f30]; +0x18 listener 0x7f0290 [ListenToMessage 0x40a250, thunk 0x40a2c0, 0x45bd80]; +0x6C 0x7f0288 [thunk
  0x40a2d0, 0x403f00]; +0x7C broadcaster 0x7f0278 [thunk 0x40a2e0, 0x45c580, 0x45c510, 0x45c470].
- Assets: none.

| off | type | meaning |
|---|---|---|
| +0xC4 | char* (heap) | next node name (copied from ctor arg 3) |
| +0xC8 | u16 | its length |
| +0xCC | timer* | 0x34-byte periodic timer (ctor 0x42fb10, period 3000 ms, message id 0x32E6); node+0x18 is registered as its listener |

| addr | name | purpose |
|---|---|---|
| 0x40a080 | ctor | HCNode ctor, copy the next-node name, create a 3000 ms timer (msg 0x32E6), listen to it |
| 0x40a1a0 | sdd | |
| 0x40a1c0 | dtor | delete the timer and the name string, then HCNode dtor 0x454880 |
| 0x40a250 | ListenToMessage(msg,param) | msg 0x32E6: pause the timer, then GoNext (0x421190). Other messages go to the HCNode listener 0x454b50 |
| 0x40a2b0/2c0/2d0/2e0 | thunks −0xC/−0x18/−0x6C/−0x7C | |
| (0x421130) | Start (shared) | reset and start the timer, then HCNode::Start 0x454c10 |
| (0x421190) | GoNext (shared) | app vt+0x1C(nextName or "", 16, 1) |

Transitions: timer expiry, mouse up (left or right) or key down → next node ("preferences"). No save.

## Preferences — factory "preferences"

The options screen shown at every boot (after BarbieSoftLogoNode). It has three two-way radio rows plus OK and
Reset, and is stored in `<HD>Prefs\Preferences.omt`. Base: HCToolbarNode.
- ctor 0x4216d0 `(app, name)`. Size **0x1FC**. Scalar deleting dtor 0x421e00. Dtor 0x421e20. Thunks
  0x4225c0/5d0/5e0/5f0.
- Vtables: main 0x7f3110. Overrides: +0x00 0x421e00, +0x48 Start 0x422010, +0x54 MouseDown 0x4220c0,
  +0x6C KeyDown 0x421ff0. +0x0C 0x7f3108 [0x4225c0, SpendTime 0x422060]; +0x18 0x7f30f8 [0x41c910 (toolbar),
  0x4225d0]; +0x6C 0x7f30f0; +0x7C 0x7f30e0.
- Globals (static ctors 0x421570/0x421580 and 0x421620/0x421630, atexit dtors 0x4215f0/0x4216a0):
  String gPrefsDir 0x80a050 = "Prefs\\", gPrefsFile 0x80a060 = "Preferences.omt". Bit flags 0x80a058.
  Global 0x80a068 = OK pressed; 0x80a06c = time OK was pressed.
- Assets: `preferences.omt.cache` and `preferences.omt` (HCAn/SMK2 musicbtn, controlsbtn, horsebtn, splash, okbtn,
  resetbtn, box1–box6; Canv/BLIT "preferences background"; WAVE DB02045/46/48/49, click, trash2); cursor
  "prefscursor".

| off | type | meaning |
|---|---|---|
| +0x1C4 / +0x1C8 | hotspot* (0x40ec40) | okbtn (id 0x3F1) / resetbtn (id 0x3F2) |
| +0x1CC | u16 | set to 1 in the ctor (meaning unknown) |
| **+0x1CE..+0x1D5** | 4×s16 | **the 8 bytes stored in Preferences.omt** (see below) |
| +0x1D8 / +0x1DC / +0x1E0 | hotspot* | labels musicbtn / controlsbtn / horsebtn |
| +0x1E4..+0x1F8 | PrefsCheckBox*[6] | box1..box6 (ids 0x3E9..0x3EE) |

### Prefs\Preferences.omt: 8 bytes, raw copy of node+0x1CE (4 little-endian int16, no header)
Read in the ctor through the OMT file stream: Exists (0x45e840), Open(read) 0x45e5a0(1,0,1),
Read(+0x1CE, 8) 0x45e870, Close 0x45e720. It is written only by SavePreferences 0x422300 when OK is pressed:
CreateDirectoryA(HD+"Prefs\\"), Open(write) 0x45e5a0(0,0,1), Write(+0x1CE, 8) 0x45e9b0, Close.

| file off | node off | meaning | values | mirrored app field (written only on OK) |
|---|---|---|---|---|
| 0 | +0x1CE | **music off** (row 1: box1 = off, box2 = on) | 1 = box1 (music off), 0 = box2 (music on) | **app+0x344** (u16) = same value. Read by the node-music selector 0x4133e0, race and trail nodes: 0 = play music |
| 2 | +0x1D0 | **mouse control** (row 2: box3 = keyboard, box4 = mouse) | 1 = box4 (mouse), 0 = box3 (keyboard) | **app+0x346** (u16) = same value, and **app+0x1A8** (u32) = the inverse (1 = keyboard control; read by race 0x424000/0x424080/0x424100 and trail ride 0x434f60/0x435400) |
| 4 | +0x1D2 | **desktop horse** (row 3: box5 = no, box6 = yes) | 1 = box6 (yes), 0 = box5 (no) | **app+0x348** (u16). On quit, toolbar 0x41e630 creates (0x410fe0) or deletes (0x411250) the "Desktop Horse.lnk" shortcut to `DesktopHorse\Horse.exe` |
| 6 | +0x1D4 | **intro voice pending** (node-internal) | set to 1 by the ctor only when the file is **missing**. Start plays db02048, db02049, db02045, db02046 and clears it, so the saved value is normally 0 | none (app+0x34A is *not* a preference: toolbar 0x41e150 sets it as the save-on-quit argument) |

When the file is missing the boxes default to box2, box4, box6 = (0,1,1), and +0x1D4 = 1. The sample files on disk
are `00 00 01 00 01 00 00 00` (defaults) and `00 00 01 00 00 00 00 00` (desktop horse off). Reset (0x3F2) also
selects box2, box4, box6 and plays "trash2". The app fields are **not** loaded from the file at startup; they change
only when OK is pressed on this node.

| addr | name | purpose |
|---|---|---|
| 0x4216d0 | ctor | Pause(1). Opens the cache. Cursor. Creates the hotspots, the six check boxes and the labels. Reads the file and lights the box for each value. Pause(0) |
| 0x421e20 | dtor | deletes the helpers and anims (musicbtn, controlsbtn, horsebtn, splash, okbtn, resetbtn, box1–6, prefscursor), ~HCToolbarNode |
| 0x422010 | Start | if +0x1D4: PlayVoice db02048, then PlayVoiceNoFlap db02049, db02045, db02046; clears +0x1D4. Does not call the base Start |
| 0x4220c0 | MouseDown | box id 0x3E9..0x3EE → toggle it and its row partner, "click". 0x3F1 OK → "click", SavePreferences, global 0x80a068=1. 0x3F2 Reset → defaults + "trash2", then the toolbar MouseDown. Others → toolbar MouseDown |
| 0x421ff0 | KeyDown | forwards to the toolbar KeyDown only when event+8==0 and event+0xC≠0 (character keys) |
| 0x422060 | SpendTime | once OK was pressed, 200 ms later → GotoNode("signin"). Then the toolbar SpendTime |
| 0x422300 | SavePreferences | box states → +0x1CE..+0x1D3 and app fields (table above); writes the file |

Transitions: OK → (200 ms) → "signin". No save-game call.

### PrefsCheckBox *(name inferred)*

Fields: +0x04 owner node, +0x08 anim (CreateAnimAtViewport "boxN", id 0x3E9+i), +0x0C u32 checked (anim
sequence = checked). Only method: scalar deleting dtor 0x421dc0, which deletes the anim by name (anim+0x100) and
frees the object.

## SignIn — factory "signin"

Player selection screen: up to 10 sign-in cards (one per savegame slot) with the player's name, buttons
New / Delete / Home(go) / Help / Quit, a big card with a text field to type a new name, and a trash
confirmation. Base: HCToolbarNode (0x41b690). Size 0x25C (factory). ctor 0x427370, dtor 0x4287b0,
scalar deleting dtor 0x4282e0.

Vtables: main 0x7F3420 (overrides +0x00 sdd 0x4282e0, +0x48 Start 0x428300, +0x54 MouseDown 0x428a00,
+0x6C KeyDown 0x429aa0, +0x80 SoundEnded 0x429cf0; Exit is toolbar 0x41e8d0); +0x0C 0x7F3418
[thunk sdd 0x429d50, SpendTime 0x429b40]; +0x18 0x7F3408 [ListenToMessage 0x4298b0, thunk sdd 0x429d60,
0x45bd80]; +0x6C 0x7F3400 [thunk sdd 0x429d70, 0x403f00]; +0x7C 0x7F33F0 [thunk sdd 0x429d80,
Broadcast, AddListener, RemoveListener].

Assets: `signin.omt.cache` (OpenResourceFile(...,1,1)); cursor anim `signincursor`; buttons (class
0x40ec40, 0xC8 bytes, anim id at anim+0x11C): `quitbtn` 2000, `homefbtn` 2001 (go), `newfbtn` 2002,
`helpbtn` 2003, `deletebtn` 2004; `signincard` (big card, id 2008); per-slot `smallcard` (via card class
0x426ee0, id 1000+slot); trash dialog `trash_bg` + buttons `trash_yes` 2006 / `trash_no` 2007; exit dialog
uses suffix "Sign" (node+0x108=3 selects "Sign" in 0x41d150) → `exit_bgSign`, `exit_yesSign`,
`exit_noSign`, `exit_creditsSign`. Fonts: Canv chunks `BureauGrotesque-FiveThree_18` and
`BureauGrotesque-FiveThree_10` (0x44dd10 with type 'Canv'). Sounds: `click`, `newgame`, `trash`,
`stable` (go). Voices: db03073 (type your name), db03071 + db03072 (welcome / help), db04015 (help),
db02044 (all slots full), db0468 (after new player created).

Key fields:
| off | type | meaning |
|---|---|---|
| +0x108 | u32 | toolbar art suffix index = 3 ("Sign") |
| +0x1C0 | u16 | new-name entry mode active |
| +0x1C4 | HC2DAnim* | `signincard` |
| +0x1C8 | TextEntry* | name text field (0x168-byte OMT string-field subclass, vtable 0x7F0CA8, ctor inlined) |
| +0x1CC | SignInNameTextStyle* | large font (18) for the text field |
| +0x1D0 | SignInCardTextStyle* | small font (10) for the cards |
| +0x1D4 | s32 | pending slot for a new player (-1 none) |
| +0x1D8 | s32 | selected slot (-1 none) |
| +0x1DC..+0x200 | SignInCard*[10] | card per slot (class 0x426ee0, 0x1C bytes) |
| +0x204..+0x228 | rollover*[10] | 0x401160 helper per card anim (mode 3) |
| +0x22C/+0x230/+0x234/+0x238/+0x23C | Button* | quit / help / delete / new / home(go) |
| +0x240 | HC2DAnim* | anim currently blinking (go button anim) or 0 |
| +0x244 | u16 | blink count; +0x246 u16 blink state; +0x248 u32 last toggle tick |
| +0x24C | u16 | 1 = no savegame exists at all (set by ctor) |
| +0x250/+0x254/+0x258 | | trash dialog bg anim / yes button / no button |

Card list construction (ctor): for slot 0..9: build path; if the file exists, LoadGame into a local
0x150-byte buffer and create the card with the buffer start as the name string (only rec+0x00 name is
displayed; no other record field is shown) and clear the "no saves" flag; otherwise create a card with
"" and hide it (card vt+4 = SetHidden). Cards get a 0x401160 rollover helper (mode 3) and are placed
with card 0x4272d0(x,y): slots 0..4 at x=20+30i, y=20+60i; slots 5..9 at x=(viewW/2-20)+30i,
y=50+60i (i=0..4). Side effects: [0x809ac0] ends at 9; [0x7ff7b4] = horse type of the last existing
save.

Methods:
| addr | name | purpose |
|---|---|---|
| 0x427370 | SignIn | open cache, cursor, 5 buttons, two text styles (inlined ctors), card list, flags |
| 0x428080 | LoadGame (static) | read savegame<slot>.brc (0x150 bytes) into rec; set current slot + desktop-horse type |
| 0x4282b6 | LoadGame$catch_all | EH funclet: free path string |
| 0x4282e0 | scalar deleting dtor | |
| 0x428300 | Start | create `signincard` (id 2008) + text field (0x168-byte string field, renderer from the 18-pt style, height 40, auto-width up to 180 px *(inferred)*), place it under the card; if no saves: entry mode, pending = first free slot, focus field, blink go, voice db03073; else hide card/field, voice db03071 then paused db03072 |
| 0x4287b0 | ~SignIn | delete trash dialog, cards+rollovers, field, fonts, buttons; delete all named anims |
| 0x428a00 | MouseDown | button dispatch (see transitions) |
| 0x429440 | SlotExists (static) | file exists for slot |
| 0x429690 | DeleteSavegame (static) | delete file + 0x404e50 cleanup |
| 0x429888 | DeleteSavegame$catch_all | EH funclet |
| 0x4298b0 | ListenToMessage | world mouse-down (-600, button 1, y<430) while in entry mode: empty name ⇒ cancel entry (hide card/field, slots -1); else lock field (read-only) |
| 0x429aa0 | KeyDown | Return in entry mode ⇒ re-activate field (field vt+0x9C); key type 1 (Esc *(inferred)*) ⇒ close trash dialog + ShowExitDialog(0) |
| 0x429b40 | SpendTime (this=node+0xC) | keep text field centred on `signincard`; toggle go-button blink each 1000 ms; if [0x80a144]>1 (second go click) ⇒ GotoNode("intromovie"); toolbar SpendTime |
| 0x429c40 | StartGoButtonBlink | blink anim = go button anim, frame 3, count 0 |
| 0x429c80 | SetGoButtonBlinkState(on) | frame 3/0; stops after 7 "on" toggles |
| 0x429cf0 | SoundEnded | `db0468` ended and [0x80a144]≠0 ⇒ GotoNode("intromovie") |
| 0x429d50/60/70/80 | thunks | this-adjust (-0xC/-0x18/-0x6C/-0x7C) to sdd |

Transitions / button logic (MouseDown, ids from anim+0x11C):
- 1000+i (card): sound click; selected=pending=i; copy card name into the field (read-only), show
  `signincard`+field, start go blink, leave entry mode. Ignored while [0x80a144]≠0.
- 2002 New: sound newgame; selected=-1; pending = first i in 0..9 with !SlotExists(i), else -1 ⇒
  voice db02044. Otherwise entry mode, show card+field with "" (editable), focus, voice db03073, blink.
- 2001 Go: ignored if trash dialog open or [0x80a144] was already non-zero (it is post-incremented).
  Selected slot ⇒ sound "stable", one SpendTime pass, `LoadGame(app+0x1B0, slot)`,
  `GotoNode("intromovie")`. Entry mode with pending slot: empty text ⇒ reset slots; else field read-only,
  selected=pending, **create record `0x409f20(name, slot)` (saves file)**, `LoadGame(app+0x1B0, slot)`,
  voice db0468, [0x80a144]=1 ⇒ intromovie when db0468 ends or on the next Go click.
- 2004 Delete: needs a selected slot ⇒ show trash dialog. 2006 trash_yes: selected slot ⇒ hide its card,
  DeleteSavegame(slot), hide card/field, clear text, sound trash; entry mode ⇒ cancel entry, sound trash.
  2007 trash_no / id 0 background: close dialog (sound click for trash_no).
- 2003 Help: voice db04015, paused db03071, paused db03072. 2000 Quit: ShowExitDialog(0) (vt+0xD0).
- 2008 big card: in entry mode with empty text ⇒ re-focus field.
- Save-game calls: only via 0x409f20 (new player). Name max length: 12 chars stored; field width is
  auto-sized up to 180 px (+0x136=1,+0x13A=0xB4) *(inferred)*; no key filter seen.

## IntroMovie — factory "intromovie"

Plays `smk\Intro.smk` (512×384, centred at 64,48). While it plays it acts as the **machine-performance benchmark**
that sets the low-end flag. Base: HCToolbarNode (0x41b690).
- ctor 0x41e990 `(app, name)`. Size **0x1C8**. Scalar deleting dtor 0x41eb90. Dtor 0x41ebb0 (where the
  benchmark is evaluated). Thunks 0x41f1b0/1c0/1d0/1e0.
- Vtables: main 0x7f1c28. Overrides: +0x00 0x41eb90, +0x48 Start 0x41f0d0, +0x6C KeyDown 0x41f080.
  +0x0C 0x7f1c20 [0x41f1b0, SpendTime 0x41ef90]; +0x18 0x7f1c10 [ListenToMessage 0x41f010, 0x41f1c0];
  +0x6C 0x7f1c08; +0x7C 0x7f1bf8.
- Assets: `intromovie.omt` (Canv "intromovie background", WAVE "Intro"); `smk\Intro.smk` played through HCMovie
  (0x453240, `new(0x124)`); `<HD path>perf.inf`.

| off | type | meaning |
|---|---|---|
| +0xC4 | u16 | toolbar edge-exit enable; cleared |
| +0x1C0 | u16 | `perf.inf` existed when the node was built. Only then is the movie skippable |
| +0x1C4 | HCMovie* | the intro movie |

Ctor: creates a 0x14-byte backdrop renderer (base 0x46b830, vtable 0x7f11a0) that installs itself through node
vt+0x24 *(inferred: plain backdrop)*. Registers this+0x18 with the mouse broadcaster 0x80a5a0+0xC. Builds
`app(0x80a384)+0xDC` (HD path) + "perf.inf"; if fopen(…,"r") succeeds it closes the file and sets +0x1C0=1. Then
calls 0x45c890(2), display->vt+0x14(0) (hide cursor *(inferred)*), SetInterval(100) (0x45cdb0) and
SetTimeMode(0) (0x45cd70).

| addr | name | purpose |
|---|---|---|
| 0x41f0d0 | Start | Creates the HCMovie and calls Open("smk\\Intro.smk", 0,0,0, noskip=1, 0, 1) through vt+0xA0. On failure it logs "Smacker File Failed to Load!" and stays on the node. Then SetPosition(64, 48, z 9000), attach (vt+0x58), movie+0xF0=0, +0x80=1 |
| 0x41ef90 | SpendTime | Stores the first-tick time in the static 0x800da8 (unused). When movie+0xF8 (finished) is set: stop and release all sounds; GotoNode(app+0x1C0==4 ? "yard" : "tackroom"). Then the toolbar SpendTime |
| 0x41f010 | ListenToMessage | If skippable (+0x1C0) and message −103 (mouse button up, from 0x45ddc0): the same GotoNode as above, then the toolbar handler |
| 0x41f080 | KeyDown | If skippable and the event character (event+0xC) ≠ 0: the same GotoNode |
| 0x41ebb0 | ~IntroMovie | **Benchmark**, see below. Then movie+0xF0=1, Hide, delete the movie, ~HCToolbarNode |

### perf.inf: 32 bytes = 8 little-endian float32, written raw with fwrite(buf, 4, 8)
The dtor calls `SmackSummary(movie->smk (+0xE0), &sum)` (Smacker 3.x SMACKSUM). It reads the registry
(app 0x44ccc0 GetRegistryInt) for `LowEndFlag_SoundFramesToDrop` (default **0**) and
`LowEndFlag_PercentDecompression` (default **30**), and converts both to float. It then opens `<HD>perf.inf`:
- **File exists** ("r"): fread 8 floats and use them (no new measurement).
- **File missing**: fopen "w", compute the values below, then fwrite all 8 floats. Floats 5–7 are never assigned,
  so the file contains **uninitialised stack bytes** there. Both the shipped `Resources/perf.inf` and the sample
  files show garbage in those slots (for example 0x00DFD000, 0x00807BD8, 1).

| idx | file off | value (u32 SMACKSUM fields converted to float; division in x87 precision) | log label |
|---|---|---|---|
| 0 | 0x00 | TotalDecompTime (+0x20) / TotalTime (+0x00) | "Decompression percent" (a fraction 0..1) |
| 1 | 0x04 | TotalBlitTime (+0x18) / TotalTime | "Drawing percent" |
| 2 | 0x08 | TotalReadTime (+0x1C) / TotalTime | "Reading percent" |
| 3 | 0x0C | TotalReadSpeed (+0x28) | "Read Speed" |
| 4 | 0x10 | SoundSkips (+0x14) | "Sound Skips" |
| 5–7 | 0x14–0x1F | uninitialised (ignore on read; write 0 in a reimplementation) | — |

Shipped disc file: (0.0879, 0.0, 0.0016, 8175260.0, 0.0, junk…).
Decision (always recomputed, from the file values or the new ones):
`lowEnd = (f[0]*100.0 > PercentDecompression) || (f[4] > SoundFramesToDrop)` (both comparisons are strict).
The result goes to **app+0x334 (u16)**. It is logged as "LowEnd Machine - %d" and written back with app 0x44cef0
SetRegistryInt("LowEndFlag_IsLowEndMachine", value). The log line "Performance Summary - …" (level 3, log.txt)
prints f[0..4].

Transitions: when the movie ends, or when skipped (only if perf.inf already existed): app+0x1C0 (player record
+0x10, horse type) == 4 → "yard", otherwise "tackroom". No save-game call.

## Yard — factory "yard" (also the factory fallback)

The barn yard. The horse can be mounted or dismounted here, and the two trail signs lead to rabbitE and beachE.
The first time the player arrives, before a horse is chosen (app+0x1C0==4), the node plays the Barbie intro instead.
Base class: HCToolbarNode. Size **0x214**.
- Constructor 0x43cda0, destructor 0x43d660, scalar-deleting destructor 0x43d640.
- Main vtable **0x7f3fb0**. Overrides:
  - +0x00 0x43d640
  - +0x48 Start 0x43ec50 (a bare jump to HCNode::Start 0x454c10)
  - +0x54 MouseDown 0x43e160
  - +0x6C KeyDown 0x43d8a0
  - +0x80 SoundEnded 0x43e6f0
  - +0x90 ScriptEnded 0x43e5f0
  - +0x98 ScriptCommand 0x43e530
- Secondary vtables:
  - +0x0C at 0x7f3fa8: [0x43ec60, **0x43e740 SpendTime**].
  - +0x18 at 0x7f3f98: [**0x43d800 ListenToMessage**, 0x43ec70, 0x45bd80].
  - +0x6C at 0x7f3f90: [0x43ec80, 0x403f00].
  - +0x7C at 0x7f3f80: [0x43ec90, 0x45c580, 0x45c510, 0x45c470].
- Assets, normal mode:
  - Resource files `yard.omt.cache` (cache), `trails.omt`, `trlAnims.omt` (both opened only when not already open,
    checked with 0x44d4b0), `trlAnims.omt.cache`, and `trailPal|Wht|Pnt|Dpl.omt` plus its `.cache`, chosen by app+0x1C0.
  - Anims `signpost`, `sign rabbit` (id 0x2711), `sign turtle` (id 0x2712), `bMountRun`, `<h>MountRun`,
    `fancyMountRun` (id 10000 = the horse), and the high-memory ambient anims `ambientBarbie`, `ambient<H>`, `ambientFancy`.
  - Optional `certificatewon_trail` (id 0x2713).
  - Scripts `mount`, `dismount`, `runOff`/`RunOff`, `Ambient`.
  - Sound `trailsign`, ambient loop `exterior`.
  - Voices db9010 (500 ms after entry), db9011, db04106 (help), db0197.
- Assets, intro mode: `yardintro.omt.cache`, anim `BarbieIntro`, script `Intro`, voices spause, pause, pause, DB04072, DB04036.
  The SFX `road10y`, `road`, `breath` and `blow` are triggered from SpendTime by BarbieIntro's frame numbers.
  `introFlaps` is deleted in the destructor.
- Globals:
  - 0x804030 (u16): intro mode.
  - 0x80a264: high-memory ambient enabled.
  - 0x80a1e0: typed-key buffer, up to 100 characters.

| off | type | meaning |
|---|---|---|
| +0x0CA | u16 | =1: clicking the left screen edge sends msg 0x32C9 |
| +0x178 | char* | ambient loop (`exterior`) |
| +0x1C0 / +0x1C4 | HC2DAnim* | `sign rabbit` / `sign turtle` |
| +0x1C8 / +0x1CC | ACRollover* | sign hotspots |
| +0x1D0 | HC2DAnim* | `certificatewon_trail` |
| +0x1D4 / +0x1D8 | obj* | certificate name text: font holder (new 0x14) / text element (new 0x168) |
| +0x1DC | HC2DAnim* | `bMountRun`, or `BarbieIntro` in intro mode |
| +0x1E0 / +0x1E4 | HC2DAnim* | `<h>MountRun` / `fancyMountRun` (0 without fancy tack) |
| +0x1E8 / +0x1EC / +0x1F0 | HC2DAnim* | `ambientBarbie` / `ambient<H>` / `ambientFancy` (0 when memory is low) |
| +0x1F4 | u16 | **high-memory flag**: GlobalMemoryStatus total physical memory > 0x112A880 (~18 MB) enables the ambient anims |
| +0x1F8 | int | destination node id from the clicked sign: 0x14 rabbitE, 0x16 beachE |
| +0x1FC | u16 | leaving to a trail (set when `runOff` ends). The destructor then keeps the trail .omt files open and sets app+0x338=1 |
| +0x1FE | u16 | horse mounted |
| +0x200 | u16 | run off as soon as `mount` finishes |
| +0x202 | u16 | fancy tack (app+0x1C4==1) |
| +0x204 | u16 | tutorial redirect pending. While set, mouse input is ignored and the end of db9011 jumps to rabbitE |
| +0x206..+0x20E | u16 | intro SFX already played. Ctor zeroes 0x1FC..0x20E. SpendTime uses its own this (node+0xC), so these are its +0x1FA..+0x202 |
| +0x210 | int | length of the typed debug string |

| addr | name | purpose |
|---|---|---|
| 0x43cda0 | Yard::Yard | Sets up both modes (see assets). Registers with the broadcasters at 0x80a5a0+0xC and +0x18. Calls the map panel (app+0x188) vt+0x14 with 0x16. If app+0x30C≠0 the horse is shown already mounted (mount anims at sequence 1, +0x1FE=1). In high-memory mode the mount anims are then hidden and the `Ambient` script runs. If app+0x320==1 it shows `certificatewon_trail` with the player name and clears the flag. Starts the `exterior` loop. |
| 0x43d640 | Yard::\`scalar deleting destructor' | |
| 0x43d660 | Yard::~Yard | Deletes the sign, intro and flap anims and stops the ambient loop. When not riding off, closes trails.omt, trlAnims.omt and trailXxx.omt; when riding off, sets app+0x338=1. Deletes the hotspots, stops `Intro` and the voice, clears intro mode, sets **app+0x30C=1**, then runs ~HCToolbarNode. |
| 0x43d800 | Yard::ListenToMessage | this=node+0x18. Ignored while +0x204 is set. Msg 0x32C9 (left edge) goes to `stable` in intro mode, otherwise to `tackroom`. Msg −0x66 (button up) with code 1 in intro mode, when node+0x19C==0, goes to `stable` (skips the intro). Everything else goes to 0x41c910. |
| 0x43d8a0 | Yard::KeyDown | Debug keyboard (see below). Event type 1 (Esc, *inferred*) calls ShowExitDialog(1) unless in intro mode. Falls through to the toolbar KeyDown 0x41c310. |
| 0x43e160 | Yard::MouseDown | Handles the horse, the trail signs and the certificate (see below). |
| 0x43e530 | Yard::ScriptCommand | `initAmbient` sets script variable hAmbient to `ambient<H>`. `initHMountRun` sets hMountRun to `<h>MountRun`. Either one also sets `fancy`=1.0 when the tack is fancy. Then calls the base 0x41dcd0. |
| 0x43e5f0 | Yard::ScriptEnded | `runOff`: sets +0x1FC=1 and goes to node name[+0x1F8]. `mount`: if +0x200 is set, starts `runOff`. Otherwise, in high-memory mode, hides the mount anims, shows the ambient anims and runs `Ambient`. Then calls the base 0x41dc80. |
| 0x43e6f0 | Yard::SoundEnded | When db9011 ends with +0x204 set, goes to `rabbitE` (forced tutorial trail). Then calls 0x41c460. |
| 0x43e740 | Yard::SpendTime | this=node+0xC. Runs the base idle 0x41cba0. In intro mode it keeps BarbieIntro shown. It plays `road10y` at frame ≥0, `road` at frame >10 and switches to sequence 2 at frame >0x22 (all in sequence 0), and plays `breath` at frame >5 and `blow` at frame >0x1C (in sequence 2). |
| 0x43e930 | Yard::ShowCertificateName | Same code as TrailNode 0x434610. Builds a font holder (new 0x14, vtable 0x7f0d98 then 0x7f33e0, font Canv `BureauGrotesque-FiveThree_18`, OMT font renderer new 0x314 via 0x469030) and a text element (new 0x168, ctor 0x465d10, vtables 0x7f0ca8 / +0x10 0x7f0ca0 / +0x1C 0x7f0c90 / +0x6C 0x7f0c88 / +0xC8 0x7f0c78 / +0xD4 0x7f0c68 / +0xE4 0x7f0c60). Sets the text to the **player name at app+0x1B0** and centres it at x=320−w/2, y=270, z = certificate z + 3. |
| 0x43ec50 | Yard::Start | Jumps to HCNode::Start. |
| 0x43ec60/70/80/90 | Yard::\`scalar deleting destructor'\`adjustor{12/24/108/124}' | |

MouseDown (+0x204 set means the click is ignored):
- **0x32CE** (help): db04106.
- **10000** (horse): only outside intro mode and when no mount, dismount or runOff script is running. If dismounted,
  runs `mount` and sets mounted=1. If mounted, shows the mount-run anims, hides the ambient anims, stops all scripts
  (0x455bb0) and runs `dismount`.
- **0x2711 / 0x2712** (rabbit / turtle sign):
  - If the tutorial is not done (app+0x254==0), sets +0x204=1 and plays db9011. When db9011 ends the game goes to rabbitE.
  - Otherwise sets +0x1F8 to 20 (rabbitE) or 22 (beachE) and plays `trailsign`. If dismounted, it mounts first
    (+0x200=1). Otherwise it switches to the mount anims and runs `runOff`. When `runOff` ends, ScriptEnded goes to the
    chosen trail.
- **0x2713** (certificate): db0197, then deletes the certificate anim and both text objects.

KeyDown debug keys:
- Typing `sevanne`+Enter plays `trash` and sets **app+0x336=1** (debug mode). `leinad`+Enter plays `camera` and clears it.
- `fr<N>`+Enter (N 1..19) sets the frame rate global 0x802790.
- '?' sets global 0x80a164 (meaning unknown).
- The other keys below need debug mode. Each one logs "YARD :: ..." through 0x419380 (yard.cpp):

| key | effect |
|---|---|
| A / a | ambient on/off (0x80279c) |
| B / b | backups on/off (0x8027a4) |
| D / d | dialog on/off (0x8027a0) |
| F / f | butterflies on/off (0x8027a8) |
| H / h | horse heads on/off (0x802794) |
| M / m | music on/off (0x802798) |
| O / o | new / old bridge (0x80a16c) |
| P / p | programmatic letterbox (0x80a170=1, 0x80a174=0) / off (both 0) |
| V / v | video letterbox (0x80a170=1, 0x80a174=1) / off |
| L | tutorial on: app+0x254=0, app+0x300=1 |
| l | tutorial off: app+0x254=1 |
| R / r | GotoNode(`rabbitE`) |

Transitions:
- Left screen edge → `tackroom` (`stable` in intro mode).
- Clicking in the intro → `stable`.
- Sign + runOff → `rabbitE` / `beachE`.
- Tutorial not done → `rabbitE`.

Saves: none.

### Referenced helper classes (defined elsewhere)
- The text element with vtable 0x7f0ca8 (0x168 bytes, OMT text-element based, dtor 0x40cef0) and the font holder with
  vtable 0x7f33e0 (base 0x7f0d98, 0x14 bytes) are also used by bulletinboard (0x40c960) and signin (0x427370/0x428300).
  Both are described in classes.md (BBNameField, SignInNameTextStyle).
- The "popup created at 0x43e930" is therefore the trail-certificate name label: one font holder plus one text element.

## Stable — factory "stable"

Horse selection and naming: overview with four horse heads (Dapple/White/Paint/Palomino), stall view with
close-up, a name book (10 pages × 6 names) and a nameplate. Base: HCToolbarNode. Size 0x25C. ctor
0x429d90, dtor 0x42a460, sdd 0x42a440.

Vtables: main 0x7F3530 (+0x00 sdd 0x42a440, +0x54 MouseDown 0x42ad00, +0x6C KeyDown 0x42aad0, +0x78
AnimEnded 0x42bf70, +0x80 SoundEnded = 0x43cd40 (shared stub → toolbar 0x41c460); Start is
HCNode default 0x454c10); +0x0C 0x7F3528 [thunk 0x42c070, SpendTime 0x42ab10]; +0x18 0x7F3518
[ListenToMessage 0x42a720, thunk 0x42c080, 0x45bd80]; +0x6C 0x7F3510 [thunk 0x42c090, ...]; +0x7C
0x7F3500 [thunk 0x42c0a0, broadcaster].

Assets: `stable.omt.cache` (1,1); `stablewhite.omt`, `stabledap.omt`, `stablepaint.omt`, `stablepal.omt`
(opened in ctor, closed in dtor). Anims: `headdap` 0x3840, `headwhite` 0x3841, `headpaint` 0x3842,
`headpal` 0x3843, `namelists` 0x3844 (book; frame = page), `nameplates` (frame = name id), `buttonleft`
0x3845, `buttonright` 0x3846, `buttonok` 0x3847, `squirrel` 0x3848, `bird` 0x3849, `bstallwalk` (Barbie
walk-in, seqs 0→1→2), `barbieAmbient`, `MouthFlapStable`, per horse stall+close-up: `DappleStall`/
`DapCloseUp`, `PaintStall`/`PaintCloseup`, `stallwhite`/`whitecloseup`, `stallpalamino`/`palcloseup`.
Sounds: `stableamb` ambient loop (vol 0x28, stored in node+0x178), horse whinnies `dapple`/`Paint`/`white`/
`Palamino`, idle `horse1..horse4`, `bookopen`, `listclose`, `pickname`, `Page`, `squirl5`, `babybird`.
Voices: greeting by visit count DB0554 (0) / DB0556 (1) / DB0557 (≥2), then DB04108; DB9003 (pick a name);
help Db04108 / DB04109; name voice "DB0%d" (below); `spause`, `pause`.

Key fields:
| off | type | meaning |
|---|---|---|
| +0x1C0 | anim | bstallwalk; +0x1C4 barbieAmbient; +0x1C8 bird; +0x1CC MouthFlapStable |
| +0x1D0..+0x1EC | rollover* | 0x401160 helpers (bird, squirrel, 4 heads, nameplates, namelists) |
| +0x1F0/+0x1F4/+0x1F8 | Button* | left / right / ok (created on first book open) |
| +0x1FC / +0x200 | anim | nameplates / namelists |
| +0x204 | s32 | name-book page 0..9 |
| +0x218 | u16 | new-player mode (app+0x1C0 was 4 on entry; no toolbar) |
| +0x21C | anim | current close-up; +0x254 current stall anim |
| +0x220/+0x224/+0x228/+0x22C | anim | heads dap / white / paint / pal |
| +0x240 | u16 | swallow next raw mouse-up after entering the stall |
| +0x248 | u32 | squirrel visible flag; +0x24C squirrel anim |
| +0x250 | u32 | stall view / book active |
| +0x258 | u32 | 1 = overview (heads) shown, 0 = stall view |

Methods:
| addr | name | purpose |
|---|---|---|
| 0x429d90 | Stable | if app+0x1C0==4: new-player mode, reset 4 name ids to -1; else toolbar buttons; load anims/omts; ambient; greeting by app+0x2F4 then ++app+0x2F4; AddListener(node+0x18) on global broadcaster [0x80a5a0]+0xC |
| 0x42a440 / 0x42a460 | sdd / ~Stable | delete buttons+rollovers, close 4 omts, delete anims |
| 0x42a720 | ListenToMessage (this=node+0x18) | -0x67: swallow; 13000 ⇒ LeaveStallView+ShowOverview; -0x66 button 1 inside (241..392, 364..393) while in stall view and book closed ⇒ open book; 0x32CA (right screen edge) ⇒ GotoNode("tackroom") if app+0x1C0≠4; 0x3841/0x3843 ⇒ SelectWhite/SelectPalomino; 0x3845-47 debug strings |
| 0x42aad0 | KeyDown | key type 1 and app+0x2F4>1 ⇒ ShowExitDialog(1); else toolbar default |
| 0x42ab10 | SpendTime (this=node+0xC) | mouth flap while voice plays; in overview every 1 s random idle: head anim + `horseN` sound, squirrel, Barbie ambient seq |
| 0x42ad00 | MouseDown | heads ⇒ set app+0x1C0 + Select*, 0x411800(type); book click ⇒ PickNameAt(y); left/right page ±1 (wrap 0..9) + "Page"; OK; squirrel; bird; help 0x32CE |
| 0x42b140 | PickNameAt(x,y) | row by y: 84–114→0, 120–150→1, 155–185→2, 192–222→3, 226–256→4, 260–290→5 (else ignore); app+0x1C8[type] = page*6+row; sound pickname; nameplate frame; voice "DB0%d" of mapped number; copy voice name to node+0x138 |
| 0x42b440 | HideOverview | hide walk, heads, squirrel; toolbar flags +0xCC=0,+0xC4=1 |
| 0x42b4b0/5f0/760/8a0 | SelectDapple/Paint/White/Palomino | stop voice, horse sound, HideOverview, page 0, create stall+close-up, app+0x1C0 = 3/2/1/0, show nameplate if named, ShowStallView, open book if unnamed, toolbar Barbie anim frame 0, app+0x1D8[type]=1 |
| 0x42b9e0 | OpenNameBook | voice DB9003, show book + buttons, sound bookopen, page 0 |
| 0x42bc70 | ShowStallView | show stall, close-up, flags |
| 0x42bcf0 | LeaveStallView | hide stall/close-up/book/buttons; if new-player mode and named ⇒ app vt+0x1C("tackroom",20,1) (GotoNode with transition 0x44de80 *(inferred)*); toolbar flags |
| 0x42bdd0 | CloseNameBook | hide book+buttons, keep nameplate |
| 0x42be60 | ShowOverview | stop horse sounds, show heads/Barbie/squirrel |
| 0x42bf70 | AnimEnded | bird reset; bstallwalk seq0→1→2→ show barbieAmbient |
| 0x42c030 / 0x42c060 | Show/HideMouthFlap | |
| 0x42c070/80/90/a0 | thunks | |

Name voice mapping (PickNameAt and toolbar SayHorseName 0x41e720): id 0..9 → 631+id; 10 → 2055;
11..35 → 632+id; 36 → 2054; 37/38/39 → 668/669/670; 40..59 → 632+id (672..691). Voice = "DB0"+number
(e.g. DB0631, DB02055).

Transitions: OK (0x3847) with a named horse ⇒ sound listclose, app vt+0x1C("tackroom",20,1), close book,
leave stall, overview; without name ⇒ nothing. Right edge 0x32CA ⇒ "tackroom". No save-game calls.

## TackRoom — factory "tackroom"

Scrolling tack-room panorama: put pad / English saddle / Fancy saddle / bridle on the horse by drag
(cursor change) and drop, feed carrot/apple/strawberry/sugar (Smacker close-up), cat, exits to grooming
(brush/comb), bulletin board, stable (sign / left edge) and yard (right edge). First visit runs the
`TackTour` script. Base: HCToolbarNode. Size 0x2A0. ctor 0x42c0b0, dtor 0x42c3f0, sdd 0x42c3d0.

Vtables: main 0x7F3640 (+0x00 0x42c3d0, +0x4C Exit 0x42d380, +0x54 MouseDown 0x42d590, +0x6C KeyDown
0x42ec30, +0x78 AnimEnded 0x42ea90, +0x80 SoundEnded 0x42f3d0, +0x98 ScriptCommand 0x42f280); +0x0C
0x7F3638 [thunk 0x42fad0, SpendTime 0x42ec70]; +0x18 0x7F3628 [ListenToMessage 0x42ea00, thunk 0x42fae0,
0x45bd80]; +0x6C 0x7F3620 [thunk 0x42faf0, ...]; +0x7C 0x7F3610 [thunk 0x42fb00, broadcaster].

Assets: `tackroom.omt.cache` (only if anim `BridleEat` not already loaded, i.e. not returning from the
bulletin board); per horse type `tackroom{pal,white,paint,dap}.omt` (+ `.omt.cache` unless already
loaded; type ≥4 falls back to paint with a debug message); SMK `smk\FeedCloseUp{Pal,White,Paint,Dap}.smk`
(path = app+0xE4 resource dir + "smk\" + name; HCMovie 0x41a300, 0x114 bytes); script `TackTour`.
Anims: `barbieAmbient`, `bridle` 0x4DA6, `bridleEat`, `brushWall` 0x4DAA, `combWall` 0x4DA9,
`bulletinBoardSmall` 0x348B, `posTR` 0x348B, `cat` 0x4DA8, `englishsaddle` 0x4DA4, `FancySaddle` 0x4DA5
(only if app+0x2A6), `pad` 0x4DA3, `horseSign` 0x4DAC, `MouthFlapTackroom`, `subtleAmbBridle`,
`TackAmbBridle`, `tackOn` 0x4DAB (frame = tack state-1), `tackroom_carrot` 0x4D9F, `tackroom_apple`
0x4DA0, `tackroom_sugar` 0x4DA1, `tackroom_strawberry` 0x4DA2, `TackAmb<H>`/`TackSubAmb<H>` 0x4DA7 (horse,
drop target), `TackFeed<H>`, `barbieFeed`; cursors `PadCursor`, `EnglishSaddleCursor`,
`fancySaddleCursor`, `BridleCursor`, `BridleSaddleCursor`, `cursorIn`. Sounds: `stableamb2` (ambient),
`Pad`, `SaddleOn`, `bridle`, `meow`, `purrs`, `getFood`, `getfood2`, `eating`. Voices: greeting DB9025
(if [0x809b00], set by 0x411810) else (no tour) pause + DB0537 (4/5) / DB0530 / DB04107 or DB9023;
feeding DB0531..DB0536; DB0565 (pad first); help Db04107; tour DB9002, toolbar, DB02050n, db0560, db0462,
db0530, DB0560, DB0540, DB0541, DB9059; DB04163 (progress 2→3).

Key fields:
| off | type | meaning |
|---|---|---|
| +0x160/+0x164 | s32 | toolbar scroll limits (1200/0; tour sets 960 / 240) |
| +0x1CC..+0x230 | anim | scene anims (see CreateScene) |
| +0x234..+0x264 | rollover* | 0x401160 helpers for clickable items |
| +0x268 | HCMovie* | feeding close-up |
| +0x270 | u32 | English saddle on horse |
| +0x27C | u32 | bridle on horse |
| +0x284 | u32 | fancy saddle on horse |
| +0x288 | u32 | pad on horse |
| +0x28C | u32 | busy (feeding) — MouseDown ignored |
| +0x290 | u32 | close-up movie playing |
| +0x294 | s32 | food being fed (1 carrot, 2 apple, 3 strawberry, 4 sugar) |
Globals (not saved): [0x80a150] tack state (0 none, 1 pad, 2 pad+English, 3 pad+Fancy), [0x80a154]
bridle on, [0x80a14c] tour running, [0x80a148] = 0 (stable sets 1), [0x80a158] = this.

Methods:
| addr | name | purpose |
|---|---|---|
| 0x42c0b0 | TackRoom | open cache, app+0x33C=0; app+0x1C0==4 ⇒ GotoNode("stable") and stop; toolbar, scroll, ambient, CreateScene, StartTackTourIfFirstVisit, greeting, ++app+0x2E4 |
| 0x42c3d0 / 0x42c3f0 | sdd / ~TackRoom | delete rollovers; app+0x30C=0; GetSound("toolbar", keep=0) (drops the keep flag set for the tour) |
| 0x42c550 | CreateScene | create anims/rollovers, horse-specific omt+anims by app+0x1C0, FancySaddle if app+0x2A6, restore tack from globals |
| 0x42d380 | Exit(next) | ReleaseAnims(next=="bulletinboard"), toolbar Exit |
| 0x42d3c0 | ReleaseAnims(keep) | 42 anim names: keep ⇒ anim vt+0x1C each *(inferred: suspend)*; else delete |
| 0x42d590 | MouseDown | tack pick-up/drop logic, food, cat, exits (below) |
| 0x42ea00 | ListenToMessage | 0x32C9 left edge & camera x<10 ⇒ "stable"; 0x32CA right edge & x>555 ⇒ "yard" |
| 0x42ea90 | AnimEnded | barbieFeed end ⇒ StartEatingPose + remember food; TackFeed seq0 end ⇒ PlayFeedCloseUp; TackFeed seq1 end ⇒ idle, busy=0; TackAmb ↔ TackSubAmb swaps |
| 0x42ec30 | KeyDown | 'F'/'f' and app+0x336 ⇒ GotoNode("finalpayoff") (debug) |
| 0x42ec70 | SpendTime (this=node+0xC) | app+0x2F8==2 ⇒ 3, voice DB04163, **SaveGame(app+0x1B0, [0x809ac0])**; close finished movie ⇒ FinishFeeding; bridle overlays follow horse anims; edge scrolling (x<20 left, x≥620 right); random idles; mouth flap |
| 0x42f200 / 0x42f230 / 0x42f260 | ScrollLeft / ScrollRight / StopScroll | toolbar scroll ±20 unless tour/movie |
| 0x42f280 | ScriptCommand | TackTour commands (toolbartour, ScrollMiddle, ScrollRight, db0560, db0462, db0530, db0800, tackTourEnded) |
| 0x42f3d0 | SoundEnded | DB9059 ⇒ tour over ([0x80a14c]=0) |
| 0x42f410 | FeedHorse(food) | not during tour: busy, barbieFeed seq=food, getFood, random voice |
| 0x42f590 | PlayFeedCloseUp | load/play FeedCloseUp<horse>.smk, sound eating |
| 0x42f7b0 | StartEatingPose | show TackFeed (+bridleEat) |
| 0x42f920 | FinishFeeding | getfood2, TackFeed seq 1 |
| 0x42fa00 | StartTackTourIfFirstVisit | app+0x2E4==0 ⇒ GetSound("toolbar", keep=1), RunScript("TackTour"), tour flag |
| 0x42fa50 / 0x42fa70 | TourScrollMiddle / TourScrollRight | |
| 0x42fa90 | IsIdle | barbieFeed idle and not busy |
| 0x42fad0/e0, 0x42faf0, 0x42fb00 | thunks | |

Tack logic (MouseDown): clicking a wall item picks it up (cursor changes; any held item returns to the
wall); picking a saddle when nothing is on the horse auto-puts the pad on (state 1). Dropping on the horse
(0x4DA7): PadCursor ⇒ state 1, sound Pad; English/Fancy cursor with pad on ⇒ state 2/3 and
**app+0x1C4 = 0 (English) / 1 (Fancy)**, sound SaddleOn; saddle without pad ⇒ voice DB0565; BridleCursor
⇒ bridle on (global + +0x27C), sound bridle. Clicking the tack on the horse (0x4DAB) swaps English↔Fancy
(writes app+0x1C4 0/1) or removes the saddle (state 1) / pad (state 0) — removal does not touch app+0x1C4.
Food: carrot 0x4D9F→1, apple 0x4DA0→2, strawberry 0x4DA2→3, sugar 0x4DA1→4. Cat 0x4DA8 ⇒ meow/purrs.
Exits: brush/comb ⇒ "grooming"; bulletin board/posTR ⇒ "bulletinboard"; horseSign ⇒ "stable"; help
0x32CE ⇒ voice Db04107. Feeding writes nothing to the record.

## Grooming — factory "grooming"

Groom the player's horse with comb and wet brush; coat-specific canvases; leaving via edge → tack room.

- Base HCToolbarNode. Factory `new(600)` (0x258). ctor 0x413940, dtor 0x413b20, scalar deleting dtor 0x413b00.
- Vtables: main 0x7f1678 (overrides +0x00, +0x54 MouseDown 0x413c40, +0x6C KeyDown 0x4140e0 (pass-through),
  +0x78 AnimEnded 0x414070); +0xC 0x7f1670 [0x415210, SpendTime 0x4140f0]; +0x18 0x7f1660 [ListenToMessage
  0x413fc0, 0x415220]; +0x6C 0x7f1658 [0x415230, 0x403f00]; +0x7C 0x7f1648 [0x415240, …].
- Assets: `grooming.omt.cache` (ctor) + per coat `groomingpal|groomingwht|groomingpnt|groomingdap.omt.cache`
  (app+0x1c0 = 0/1/2/3, default paint); anims `Beam` (hotspot 0x5B82), `posG`, TackGroomBrush (0x5B7F),
  TackGroomComb (0x5B7E), TackGroomBodyDark<Coat>, TackGroomBodyLight<Coat>, TackGroomBodyLight<Coat>2,
  GroomHead<Coat>Amb (0x5B81), GroomTail<Coat>Amb, cursor anims TackGroomComb<Coat>, TackGroomTailComb<Coat>,
  TackGroomCombCursor, TackGroomBrushWet, TackGroomBrushWetUp (also Mask/Bucket/CombWall deleted in dtor);
  sounds stableamb2 (ambient, vol 40), brushing, bubbles2, Whinney1–5, Snort; voices Pause, DB03058, sPause,
  DB0540, Db04110, DB02053, DB0542–DB0545, DB00546.

| off | type | meaning |
|---|---|---|
| +0x108 | u32 | toolbar field, 0 in ctor (carousel sets 1) |
| +0x244 | u32 | mouse button held (ListenToMessage −0x66 → 1, −0x67 → 0) |
| +0x178 | char* | current ambient sound name ("stableamb2") released on change |
| +0x1C4 | u32 | tool state: 0 none, 2/3 comb (3 = on horse), 4/5 wet brush (5 = on body) |
| +0x1C8 / +0x1E8 | char[0x20] | coat-specific comb / tail-comb cursor anim names |
| +0x208 / +0x20C / +0x210 | anim | body light / body light2 (hidden) / body dark |
| +0x214 / +0x218 | anim | head ambient (0x5B81) / tail ambient |
| +0x21C | anim | comb cursor anim (hidden) |
| +0x220 / +0x228 | anim | comb / brush tool anims |
| +0x230 / +0x234 | anim | Beam (0x5B82) / posG (placed at −274,−64) |
| +0x238 / +0x23C | ACRollover* | comb / brush rollovers (deleted in dtor) |
| +0x248..+0x254 | float | last mouse positions (reset to −1.0 on button up) |

Globals: [0x809c24] grooming sub-state, [0x809c20] last-hint tick (30 s nag), [0x7ffff0] / [0x7ffff4]
one-shot first-use voices (initially 1), [0x809c18]/1c last mouse x/y, [0x809c0c] last shine tick.

| addr | name | purpose |
|---|---|---|
| 0x413940 | Grooming::Grooming | open cache, toolbar "In" buttons, cursorIn, ambient stableamb2, CreateCoatAnims, intro voices, register listener |
| 0x413b00 / 0x413b20 | dtors | delete rollovers, release sound, delete anims, toolbar dtor |
| 0x413c40 | MouseDown | 0x5B7E pick comb (state 2, voice DB0540); 0x5B7F pick brush (state 4, DB02053); 0x5B81 head click with x>470 → head anim seq 1 + random whinny/snort; 0x5B82 put tool back (cursorIn, state 0); 0x32CE help → Db04110 |
| 0x413fc0 | ListenToMessage (+0x18) | −0x67: release, stop `brushing`; −0x66: held=1; 13000 → GotoNode("tackroom") |
| 0x414070 | AnimEnded | head/tail ambient ended → sequence 0 |
| 0x4140e0 | KeyDown | toolbar KeyDown |
| 0x4140f0 | SpendTime (+0xC) | tool logic: comb on head/mane/body/tail (cursor swaps, `brushing` sound while moving, first-use voice DB0543), wet brush on body (PaintWetBrush, voices DB0544/DB0545/DB0542), 30-s hints; x>620 with no button drops the tool; then UpdateShine and toolbar SpendTime |
| 0x4147d0 | CreateCoatAnims | per-coat cache and anims, hotspots, rollovers |
| 0x414e30 | PaintWetBrush | `bubbles2` sound; copies a 40×18-pixel block around the cursor into the body canvas |
| 0x415020 | UpdateShine | every ≥60 ms copies 150 random 4-byte pixel runs between body-light2 and body canvases *(inferred: gradual shine)* |
| 0x415210/0x415220/0x415230/0x415240 | thunk dtors | → 0x413b00 |

Transitions: edge (13000) → `tackroom`. No save calls.

## BulletinBoard — factory "bulletinboard"

Its popup panels (BBPanel, BBClueBook, BBCertificates, BBTeamPhoto, BBMembershipCards, Handbook and the handbook
pages) are described in [classes.md](classes.md#bulletin-board-panels-and-the-handbook); the clue-book and certificates-book
"Go" buttons are what start missions M1–M8 and C1–C4.

The club bulletin board (reached from the tack room). It holds buttons that open overlay panels: the handbook, the
certificates book, the clue book, the team photo and the membership card (rider rank). Base: HCToolbarNode. Size 0x1F8.
ctor 0x40def0, dtor 0x40e970, sdd 0x40e270.
- Vtables: main 0x7f0df8 (56 slots): +0x00 sdd, +0x48 Start 0x40e2d0, +0x4C Exit 0x40eb60, +0x54 MouseDown 0x40e410,
  +0x80 SoundEnded 0x40e730, +0xD0 ShowExitDialog 0x40e290, **+0xDC OpenPanel(BBPanel*) 0x40e240** (new virtual).
  +0xC 0x7f0df0 [thunk 0x40ec00, toolbar SpendTime 0x41cba0]; +0x18 0x7f0de0 [ListenToMessage 0x40e7c0, thunk 0x40ec10, 0x45bd80];
  +0x6C 0x7f0dd8 [thunk 0x40ec20, 0x403f00]; +0x7C 0x7f0dc8 [thunk 0x40ec30, …].
- Assets: `bulletinboard.omt.cache` (opened only if anim `certificatebtn` is not already loaded); cursor `cursorIn`; toolbar
  tb*In 0..6. Buttons (HCRolloverButton subclass 0x401000, 0xD4 bytes): `handbookbtn` id 10000, `certificatebtn` 0x2711,
  `cluebtn` 0x2712, `photobtn` 0x2713 (created in Start), rider button 0x2714 = `secretvalleyriderbtn` / `moonriderbtn` / `starriderbtn`
  by app+0x2BC. Sounds `bookopen`, `bookclose`, `close`. Voices db9000/db9001/db9004 (first visit), db9059, db0408, db0191,
  db0476/db0469/db0198 (idle hints), `pause`.

| off | type | meaning |
|---|---|---|
| +0x1BC | u16 | (toolbar) pending DB9011 → rabbitE |
| +0x1C0 | BBPanel* | extra panel slot (deleted in the dtor, never created here) |
| +0x1C4 | Handbook* | handbook panel (ctor 0x417d40, 0x6C bytes), lazily created |
| +0x1C8 | BBClueBook* | (0x28 bytes) |
| +0x1CC | BBCertificates* | (0x58 bytes) |
| +0x1D0 | BBMembershipCards* | (0x20 bytes) |
| +0x1D4 | u16 | after closing the current panel, re-open the handbook (set by handbook code 0x418460) |
| +0x1D6 | u16 | a panel was just opened: swallow the next msg −0x67 |
| +0x1D8 | BBPanel* | current/visible panel |
| +0x1DC | HC2DAnim* | anim deleted in the dtor (never set here) |
| +0x1E0 | BBTeamPhoto* | (0x10 bytes) |
| +0x1E4/+0x1E8/+0x1EC/+0x1F0/+0x1F4 | button* | handbook / certificate / clue / rider / photo buttons |

| addr | name | purpose |
|---|---|---|
| 0x40def0 | ctor | toolbar ctor; open the cache; global 0x809ad8 (panel "DB9011 pending") =0; toolbar setup; create 4 buttons; **rank update: count non-zero dwords app+0x278..+0x284 (the 4 certificates); 2 → app+0x2BC=1 (Moon Rider), 4 → 2 (Star Rider); otherwise unchanged**; pick the rider-button art from app+0x2BC; register with broadcaster 0x80a5a0; node+0xC4=1 |
| 0x40e240 | OpenPanel(p) | Hide the current panel, current=p, p->Show |
| 0x40e270 | sdd | |
| 0x40e290 | ShowExitDialog(int) | close the visible panel, then toolbar 0x41e150 |
| 0x40e2d0 | Start | **app+0x2C4++** (first visit: db9000+db9001+db9004, else db9059); create photobtn; set the app+0x19C timer period to 450000 ms and restart it (idle-hint timer *(inferred)*); does not call HCNode::Start |
| 0x40e410 | MouseDown | if a panel is visible → panel->MouseDown and return. Otherwise by id: 0x32CE → db9000/9001/9004; 10000 → handbook (lazily) + Show + `bookopen`; 0x2711 → certificates; 0x2712 → clue book; 0x2713 → team photo, **app+0x2C8++** (first: db0408); 0x2714 → membership card. Each sets +0x1D6=1. Then toolbar MouseDown |
| 0x40e730 | SoundEnded(name,loop) | if +0x1BC and name=="DB9011" → clear, GotoNode("rabbitE"); else forward to the visible panel's slot 1; then HCNode 0x457010 |
| 0x40e7c0 | ListenToMessage | −0x67 while +0x1D6: swallow once. **13000**: if a panel is visible → (if handbook: `bookclose`) hide it; if +0x1D4, re-show the handbook; voice `pause`. No panel → sound `close`, **GotoNode("tackroom")**. **50000** (idle hint): if no voice is playing → db0191 (certificates open) or random db0476/db0469/db0198. Else toolbar 0x41c910 |
| 0x40e970 | dtor | pause the app+0x19C timer, unregister from 0x80a5a0, delete panels and buttons (button anim ptr zeroed first), node+0xC4=0, toolbar dtor |
| 0x40eb60 | Exit(next) | 0x42d3c0(next=="tackroom") (tack-room asset keep/purge *(inferred)*), then toolbar Exit 0x41e8d0 |
| 0x40ec00/10/20/30 | thunks −0xC/−0x18/−0x6C/−0x7C | |

Transitions: close with no panel open → "tackroom". After choosing a mission while app+0x254==0: voice DB9011 → "rabbitE"
(through BBClueBook/BBCertificates::SoundEnded and global 0x809ad8). Otherwise → toolbar mode 3 (0x41c760(3), shows the app+0x188 object = map *(inferred)*).

## TrailNode — base of appleW, cottageL, bridgeS, rainbowS, rabbitE, beachE, wishingL

Purpose: plays a first-person horseback ride as a Smacker movie (the "trail"), with a horse-head overlay that turns
and jumps, speed control, obstacles to jump, forks chosen by steering or by clicking signs, and "stops" where Barbie
can dismount, walk, feed the horse and click mission props. One node object can switch between the 8 trail movies
without leaving the node (map jumps between trail locations are handled in-node).

- Base: HCToolbarNode (base ctor 0x41b690, base dtor 0x41bc30).
- Ctor `TrailNode(app, nodeName, trailId)` 0x431420 (3 stack args; Ghidra shows 2). Dtor 0x431fa0, scalar deleting
  dtor 0x431f60. Size **0x7fc** (subclass factories `new(0x7fc)`; last field used +0x7f8).
- Subclass ctors → trail id: appleW 0x40a2f0 → 0 (also forces node+0x7ec = 0), beachE 0x40a3a0 → 5,
  cottageL 0x40a450 → 1, bridgeS 0x40a500 → 2, rabbitE 0x40a5b0 → 4, rainbowS 0x40a660 → 3, wishingL 0x40a710 → 6.
  Each also writes app+0x338 = 0. Their vtables (0x7f03a0, 0x7f04b0, 0x7f05c0, 0x7f06d0, 0x7f07e0, 0x7f08f0, 0x7f0a00)
  differ from TrailNode's only in slot +0x00 (own scalar-deleting dtor).

### Vtables
| vptr at | vtable | slots overridden by TrailNode |
|---|---|---|
| +0x00 | 0x7f39f8 (55 slots, no new virtuals) | +0x00 0x431f60 dtor, +0x48 Start 0x431f80, +0x54 MouseDown 0x432600, +0x6C KeyDown 0x434f60, +0x70 KeyUp 0x435400, +0x7C AnimFrameCue 0x43b180, +0x88 ScriptStarted 0x43aa40, +0x8C ScriptPreempted 0x43b000, +0x90 ScriptEnded 0x43aab0, +0x98 ScriptCommand 0x43a080, +0xD8 GotoNodeById 0x439e20. Inherited: +0x4C Exit 0x41e8d0, +0x58 MouseUp 0x41c450, +0x78 AnimEnded 0x41c250, +0x80 SoundEnded 0x41c460, +0xC8..+0xD4 toolbar. |
| +0x0C | 0x7f39f0 | [0] dtor thunk 0x434c70 (ecx-=0xC), [1] SpendTime 0x435980 (this = node+0xC) |
| +0x18 | 0x7f39e0 | [0] ListenToMessage 0x435510 (this = node+0x18), [1] dtor thunk 0x434c80, [2] 0x45bd80 inherited |
| +0x6C | 0x7f39d8 | [0] dtor thunk 0x434c90, [1] 0x403f00 inherited |
| +0x7C | 0x7f39c8 | [0] dtor thunk 0x434ca0, [1..3] broadcaster 0x45c580/0x45c510/0x45c470 inherited |

Offsets inside SpendTime and ListenToMessage are relative to the sub-object; all offsets below are node-relative.

### Assets
- OMT opened in ctor (skipped if already open, 0x44d4b0): `trails.omt`, `trlAnims.omt`, cache `trlAnims.omt.cache`,
  per-horse `trailPal/trailWht/trailPnt/trailDpl.omt` (table 0x8027d0, index app+0x1c0) plus its `.cache`;
  mission omts through 0x431050 (mission1/3/5/6.omt, cert1.omt —). Dtor closes trails, trlAnims and
  horse omt (only when app+0x338 == 0).
- Trail movies (`<app+0xe4>smk\` + name; `_L` = low-res 300x152 variant, scaled to 600x300), table 0x802f68
  `[trail*2 + lowres]`: AppleW, CottageW, Bridge, Rainbow, RabbitE, TurtleE, WishingN, WishingS (.smk / _L.smk).
- Trail palettes (`.act`, loaded by 0x431200, merged with horse palette entries 0xC0–0xFF by 0x431380):
  trail table 0x802fa8 aa_west, cw_loop, ob_south, rf_south, rr_east, tr_east, ww_north, ww_south; horse table
  0x802fc8 pal, wht, pnt, dpl.
- Horse-head overlays `smk\horseheads\` (tables `[horse*2 + lowres]`): straight 0x802fd8 (`<h>_straight_crop` /
  `<h>_fwd_L`), left 0x802ff8, right 0x803018, jump 0x803038, reverse 0x803058 (`<h>_rev_crop` / `<h>_rev_L`).
  On-disc names differ in case only.
- Backup (refusal) movies: `smk\<prefix> back0|1|2[_L].smk` for trails 0,3,4,5 (prefix table 0x802f48:
  aaW cwL obS rfS rrE trE wwE wwW).
- Insert movies `smk\<prefix> <name>[_L].smk` via 0x439160: Sparkle, HoofPrints, Cottage, HorseShoe, FallenBird,
  Branches. Deer overlay `smk\deer.smk` (trail 0). Butterflies `smk\butterflies\<aa_|cw_|..><species><_20|_50|_80>[REV].smk`
  (species table 0x8030e0 `[trail*3 + rand%3]`: admiral/frit/yellow/Baton/Swallow/Yellow/Apollo, "" = none).
- Anims (in omts): `<p>t decision0..4`, `<p>t generic0` (prefix with 3rd char replaced by 't': aat, cwt, obt, rft, rrt,
  trt, wwt), props `aat rr sign`, `rrt rr sign`, `rrt ob sign`, `owl`, `cow cud`, `sandees cow`, `mailbox`,
  `m6 bush`, `m4 bush`, `m4 gate`, `m4 sparkle`, `wishing well`, `m6 coin toss`, `m6 coin cursor`, `M7 bell`,
  `m7 bell`, `M7 nobell`, `bell grass`, `bridge rail`, `obt dgrass2`, `obt ggrass0`, `cottageGrass`; horse/Barbie
  anims listed under the state machine.
- Scripts (.scr in trails.omt, names case-insensitive): preload, runOn, RunOn, runOff, RunOff, runLeft, RunLeft,
  RunRight, mount, dismount, Ambient, LookLeft, ExcitedLook, HorseAmb, WalkAmb, WalkLookLeft, WalkBack, Walk,
  TMoment2, FeedApple, PagerDelay.
- Sounds: gait loops `<surface><2|8|10|12|15>` (surface per trail table 0x7f3df0 → grass/road/rocks/sand; suffix
  table 0x803078), ambient loops wind/Xaat001/Xrrt001/Xtrr001/Xwwt001 (index table 0x7f3c50), `falls`, `stream`,
  `whinney`, `whinney2`, `feedback` (jump), `barbDirt`, `cowchew`, `cow2`, `owl`, `owlfly`, `mailOpen`, `mailClos`,
  `musicbox`.
- Music (channel 13 via 0x4133c0), id table 0x7f3c30 per trail: 1 nonmissionMusic, 2 trailMusic, 3 hiddenMusic,
  4 MysteryMusic.
- Voices: db0197, db0198, db02001, db0009, db0018, db0090, db0516, db0519, db0598, db0599, db0623, db04058,
  db04060, db04064, db04153, db03066, db9013, db9050, db9051, db9061, db9062, db0453, db0457, db0458.

### Static tables (rdata)
| addr | layout | meaning |
|---|---|---|
| 0x7f3830 | [8 trails][5] (u32 lo, u32 hi) | decision windows in trail-frame units; 9999 = unused |
| 0x7f3ae0 / 0x7f3b40 / 0x7f3ba0 | [8][3] u32 | obstacle window start / end frame, and base-movie seek frame used when the horse refuses |
| 0x7f3c10 | [8] u32 | default next trail when a trail ends: 4,4,0,0,3,0,4,4 |
| 0x7f3c30 | [8] u32 | music id per trail: 2,3,2,2,2,2,3,3 |
| 0x7f3c50 | [8] u32 | ambient loop index per trail: 1,4,2,2,2,3,4,4 (mission 4 forces 0 "wind") |
| 0x7f3c70 | [8][3 faces][4] (s16 lo, s16 hi) | frame ranges where a head direction is invalid (straight checks faces 0 and 2) |
| 0x7f3df0 | [8] u32 | ground surface per trail (0 grass,1 road,2 rocks,3 sand): 1,0,2,2,1,3,0,0 |
| 0x8027b0 | [8] u32 | map location index per trail: 0,1,2,3,4,5,6,6 |
| 0x8038f8 | [14] char* | horse-state names (debug) |

Decision windows (lo–hi) and obstacles (start–end → refusal seek) per trail:

| trail | d0 | d1 | d2 | d3 | d4 | obstacles |
|---|---|---|---|---|---|---|
| 0 aaW | 730–770 | 555–650 | 47–245 | 300–345 | 365–555 | 510–525→513, 565–612→600, 626–665→653 |
| 1 cwL | 60–145 | – | – | – | – | – |
| 2 obS | 40–136 | 137–175 | 0–40 | – | – | – |
| 3 rfS | 40–145 | – | – | – | – | 85–121→111, 140–199→187, 227–259→248 |
| 4 rrE | 60–180 | 300–430 | 475–550 | 770–820 | 842–975 | 108–156→144, 192–266→254, 302–383→370 |
| 5 trE | 80–140 | 450–545 | – | – | – | 170–215→203, 270–302→290, 320–361→350 |
| 6 wwE | 170–200 | – | – | – | – | – |
| 7 wwW | 0–25 | – | – | – | – | – |

### Key fields (node offsets)
| off | type | meaning |
|---|---|---|
| +0x1C0 / +0x1C4 | int | left / right mouse button released flags (mouse-button speed mode) *(inferred)* |
| +0x1C8 | u32 | GetTickCount of last input / last stop (5-minute idle nag) |
| +0x1CC | char[] | `<app+0xe4>smk\` ; +0x250 char[] = that + `horseheads\` |
| +0x2D4 | HCMovie* (0x130 B, ctor 0x41ab50) | base trail movie; movie+0x104 = current fps (2..max), +0xFE paused, +0x102 finished, +0x11C/+0x12 pan, +0x11E vertical offset, +0x12C palette |
| +0x2D8 | HCMovie*[6][3] | backup movies `[trail*3+i]` (trails 0,3,4,5) |
| +0x320 | HCMovie* | insert movie (Sparkle, HoofPrints, ...) |
| +0x324 | HCMovie* | current movie (base, backup or insert) |
| +0x328 / +0x32A | s16 | horizontal head/pan offset (0..0x50, centre 0x28; pan = (x−0x28)*3) / vertical offset (jump arc, rest 0x28) |
| +0x32C | int | mount anims not loaded yet (first stop calls LoadMountAnims) |
| +0x330 | u16 | **riding** (horse moving, movie playing); 1 after ctor |
| +0x332 | u16 | RunOn pending after runOff |
| +0x334 | u16 | mount when the Walk script ends |
| +0x336 | u16 | ride off ("RunOff") when the mount script ends |
| +0x338 | u16 | Barbie walking disabled |
| +0x33A | u16 | inside an obstacle window (UpdateObstacle each tick) |
| +0x33C/+0x33E/+0x340/+0x342 | u16 | segment-skip flags (see trail graph) |
| +0x344/+0x348/+0x34C/+0x350 | overlay* | head overlays on base movie: left (x 0x9F), straight (x 0xC7), right (x 0xD6), jump (x 0xB9); y = 480 (390 low-res) − height |
| +0x354..+0x360 | overlay* | same four heads attached to the insert movie |
| +0x364 | overlay*[18] | reverse heads for backup movies (x 0xC4) |
| +0x3AC / +0x3B0 | int | current music id / ambient loop index |
| +0x3B4 / +0x3B8 | int / u16 | gait index 0..4 / gait loop playing |
| +0x3BC / +0x3C0 | sound* | `falls` / `stream` |
| +0x3C4 | int | copy of global pause [0x80a438] |
| +0x3C8 / +0x3CC | list* / int | preload name queue (12-byte pool nodes from 0x80a178..0x80a188) / count; consumed by script command preload_getnext |
| +0x3D0 / +0x3D2 / +0x3D4 | u16 / u16 / overlay* | deer done / deer visible / deer overlay (y 0x5A) |
| +0x3D8 | HC2DAnim* | decision anim |
| +0x3E0 / +0x3E4 / +0x3E8 | int | head face (0 left, 1 straight, 2 right) / face-check countdown (0x19) / current obstacle index |
| +0x3EC / +0x3F0 | u16 / int | jump in progress / jump phase |
| +0x3F4/+0x3F6/+0x3F8/+0x3FA | u16 | keys held: slow down (code 9), speed up (8), steer left (6), steer right (7) |
| +0x3FC / +0x3FE | s16 | walk target (mouse x,y) |
| **+0x400** | int | **current trail id 0..7** |
| **+0x404** | int | **requested next trail id** |
| **+0x408** | int | start frame for next trail |
| +0x40C | u16 | restart same trail (map jump to the current location) |
| +0x410 | palette*[8] | per-trail .act palettes; +0x430 working palette; +0x434 horse palette |
| +0x438 | int | mission id cached at HorseStop (used by ClearStopProps) |
| +0x43C | u16 | start frame already preset (skip StartFrame) |
| +0x440 | int | horse state 0..13 (ctor 13 NO_STATE) |
| **+0x444** | u16 | **high-memory flag**: GlobalMemoryStatus dwTotalPhys > 18,000,000 → ambient, walk and tender-moment anims allowed |
| +0x446 | u16 | Ambient-preempt toggle |
| +0x448..+0x498 | HC2DAnim* | horse/Barbie anims: 0x448 bMountRun, 0x44C <H>MountRun, 0x450 barbieWalkOutside, 0x454 SaddleAndReigns, 0x458 GeneralAmb<H>, 0x45C SubtleAmb<H>, 0x460 GeneralAmbSdl, 0x464 SubtleAmbSdl, 0x468 ambientBarbie, 0x46C ambient<H>, 0x470 barbie_ambients, 0x478 bTMoment2, 0x47C hTMoment2 <H>, 0x480 sTMoment2, 0x488 fancyMountRun, 0x48C GeneralAmbFancy, 0x490 SubtleAmbFancy, 0x494 ambientFancy, 0x498 fTMoment2 |
| +0x476 | u16 | run TMoment2 after HorseAmb |
| +0x484 | u16 | fancy tack (app+0x1c4 == 1) |
| +0x49C | u16 | butterflies enabled |
| **+0x4A4** | u16 | **Barbie mounted while stopped** |
| +0x4A8/+0x4AC/+0x4B0/+0x4B4 | int | walk cycles / walk direction (0 right, 1 left) / last direction (2 = just dismounted) / Barbie slot 0..4 (1 = at saddle, 2 = at the horse's head) |
| +0x4B8 | char* | ambient loop name |
| +0x4BC | char[8][5][10] | gait sound names |
| +0x66C | u16 | ride voice hint played |
| +0x670..+0x68C | | butterfly overlays _20/_50/_80, active, step, stage, x dir (±1, +1 = REV), y dir |
| +0x690/+0x694 | anim* | road signs (aat/rrt rr sign, rrt ob sign); +0x698/+0x69C their rollovers |
| +0x6A8/+0x6AC, +0x6B0/+0x6B4 | | owl + rollover, sandees cow + rollover |
| **+0x6B8** | int | **decision index at current stop** (0..4, 5 = none) |
| +0x6BC / +0x6C0 | anim* / int | pick-up item anim (click 0x2714) / its bag item id |
| +0x6C8 | u16 | first-ride tutorial finished |
| +0x6CC | int | node id for the 0x2711 exit prop (set by mission handlers: 0 applesort, 9 vinegame, 12 carousel) |
| +0x6D8 / +0x6DC / +0x6E0 | | certificate anim ("certificatewon_*") / font object / name text element |
| +0x6E4/+0x6F0 | | mailbox + rollover |
| +0x728/+0x72A/+0x72C | u16 | HoofPrints insert already shown (trail 4 / 0 / 5) |
| +0x730/+0x740 | | m4 gate + rollover; +0x754 m6 bush, +0x758 wishing well, +0x764/+0x768 rollovers |
| +0x774 | u16 | db03066 played |
| +0x780 | obj* | modal popup; blocks space/continue; deleted when its +0xF8 flag is set |
| +0x7BE | u16 | HorseRestart drops the insert movie |
| +0x7EC | int | map location index (0x8027b0[trail]) |
| +0x7F4 / +0x7F8 | int / u16 | pager delay ms (7000 / 8000) / pager busy |

Rollovers are created with `new(0xA0)` + 0x401160(node, anim, cursor 7 or 8) (helper class vtable 0x7f0020: anim hot-spot
that changes the cursor; it increments [0x809930], which HorseRestart checks for leaks). Clickable anims carry
their id in anim+0x11C.

### Horse state machine (SetHorseState 0x43b240, field +0x440, names at 0x8038f8)
| id | name | visible after the switch |
|---|---|---|
| 0 | MOUNT_RUN | bMountRun (+ <H>MountRun, fancyMountRun when high memory is off) |
| 1 | AMBIENT | ambientBarbie, ambient<H>, ambientFancy |
| 2 / 3 | TURN_LEFT / TURN_RIGHT | bTurnLeft/<H>TurnLeft/fTurnLeft or the Right set |
| 4 | LOOK_LEFT | bLookLeft, <H>LookLeft, fLookLeft |
| 5 | EXCITED_LOOK_LEFT | bExcitedLook, <H>LookLeft, fLookLeft |
| 6 | SUBTLE_AMBIENT | SubtleAmb<H>, SubtleAmbSdl, SubtleAmbFancy (general ones hidden) |
| 7 | GENERAL_AMBIENT | GeneralAmb<H>, GeneralAmbSdl, GeneralAmbFancy |
| 8 | WALK | barbieWalkOutside (low memory: + static horse <H>MountRun, fancyMountRun, SaddleAndReigns) |
| 9 | WALK_AMBIENT | barbie_ambients (walk anim hidden) |
| 10 | TENDER_MOMENT | bTMoment2, hTMoment2 <H>, sTMoment2, fTMoment2 |
| 11 | FEED_APPLE | bFeedApple (placed at bTMoment2.x−3), <H>EatApple |
| 12 | HIDE_ALL | nothing |
| 13 | NO_STATE | initial value only |

Per-horse anim tables (index app+0x1c0, Pal/Wht/Pnt/Dpl): ambient 0x803868, MountRun 0x803878, LookLeft 0x803888,
TurnLeft 0x803898, TurnRight 0x8038a8, GeneralAmb 0x8038b8, SubtleAmb 0x8038c8, `hTMoment2 <H>` 0x8038d8,
EatApple 0x8038e8. Hiding uses ForceHideAnim, which calls anim vt+0x30(1) and vt+0x38(1) until the anim's
counters at +0x48 and +0x5C are nonzero. ForceShowAnim does the opposite.
States are set from ScriptCommand (scripts call into the node): initAmbient→1, initHMountRun→0, initHLookLeft→4 (or
5 for ExcitedLook), initHTurnLeft→2, initHTurnRight→3, initHorseAmb / hideHorseForSubtle→6, hideHorseForGeneral→7,
initLookLeft→4, initTMoment2→10, initWalk→8, initWalkAmb→9. They are also set from ScriptStarted("runOff")→0,
ScriptEnded("RunLeft"/"RunRight")→12, ("runOn")→0, ("FeedApple"/"TMoment2" on low memory)→8, and from
ScriptPreempted.

ScriptCommand also sets script variables: `fancy`=1, `hAmbient`/`hMountRun`/`hLookLeft`/`hTurnLeft`/`hTurnRight`/
`generalHorseAmb`/`subtleHorseAmb`/`hTMoment2`/`hEatApple` = per-horse anim name, `delay` (ExcitedLook 500,
initM5Payoff 4000, initPagerDelay = +0x7F4), PagerOKToGo → `talking` (voice playing) and `busy` (+0x7F8), initWalk →
`cycles`/`direction`/`lastDirection`. moveLeft −66, moveRight +98, moveCLeft −132, moveCRight +132, resetLeft −66
and resetRight +34 move barbieWalkOutside in x. preload_getnext → `preload.animName` = next queued name (or "").
Everything else is passed to the toolbar 0x41dcd0.

### Ride loop (SpendTime 0x435980, this = node+0xC)
Toolbar idle first. While the global pause is set it only copies the flag. On unpause it restarts gait and ambient sounds.
app+0x33A cancels a stop request, and app+0x33C or app+0x33E triggers HorseStop. When stopped it plays a voice nag
after 5 minutes idle, calls mission-5 tick 0x443510, and tears down the modal popup at +0x780. When riding, in order:
water sound volumes (falls: trail 3, peak at frame 80, stops at 160; stream: trails 0/5, swells over 40 frames before
the obstacle end, stops 160 frames later); segment skips; deer overlay (trail 0, shown at frames 167–179, removed at ≥231);
trail 7 auto-jump at 141–149; trail 1 auto-stop at 81–88 (seek 95 and HorseStop); steering branches (face held >5
ticks); end-of-trail (TrailEnded); insert movies; obstacle window → UpdateObstacle; jump arc (100 ms steps) or speed and
steering input; gait sound from fps (fps<4→0, <7→1, <10→2, <15→3, else 4); head face; and once per new frame: 0x430ad0
(random trail events), butterflies (start when frame%75==0 && rand%3, then 8/11/10-frame stages moving
20 px per frame), ride hints db03066/db9051/db9050, and the per-frame mission hook.
Input modes (app+0x1a8): 1 keyboard (keys 6/7 steer ±1 per ~100 ms, 8 faster, 9 slower); 0 mouse buttons (left held =
faster, right held = slower, every 200 ms; double-click left (<1 s) = HorseJump, double-click right = HorseStop);
2 mouse motion (vertical move >100 px = faster/slower, then cursor recentred). In modes 0 and 2, horizontal mouse
motion nudges the pan, and a 300 px swing turns the head left/right and recentres the cursor (SetCursorPos 320,240).

Obstacles (UpdateObstacle 0x4372f0): if the jump flag is not set when fewer than 5 frames remain before the obstacle end,
the base movie seeks to the refusal frame, pauses, plays `whinney2`, and plays `back<i>` with the reverse head. Pressing
jump after the backup ends resumes the base movie. When jumping, the jump head shows and +0x32A follows an arc over the
last 12 frames: 0x28,0x20,0x18,0x10,8,0, then back up (voice db0457/db0458 at the apex during the tutorial).

### Trail graph
Trail ids: 0 aaW AppleW (node appleW), 1 cwL CottageW (cottageL), 2 obS Bridge (bridgeS), 3 rfS Rainbow
(rainbowS), 4 rrE RabbitE (rabbitE), 5 trE TurtleE (beachE), 6 wwE WishingN (wishingL), 7 wwW WishingS (no node;
reached from 6). Frame counts: 831, 199, 293, 420, 1132, 608, 196, 204.

| from | event | to (start frame) |
|---|---|---|
| 0 aaW | base frame 789 reached (TrailEnded, movie not finished) | **GotoNodeFade("yard",16,1)** (app vt+0x1C 0x44de80) |
| 0 aaW | face right held at frames 746–749 → seek 793 | end of movie → 4 (0x447) |
| 0 aaW | stop at d0, click "aat rr sign" (0x2712) → RunRight | 4 (0x447) |
| 0 aaW | mission 0xB at end | also C4LoadTrail(1) (0x440040) |
| 1 cwL | movie end | 4 (0x408) |
| 2 obS | movie end | 0 (0x164) |
| 3 rfS | movie end | 0 (0x38) |
| 4 rrE | frame 1093 (unless entered at 0x447, flag +0x342) | 3 (0) |
| 4 rrE | movie end | loop: seek 83, clear +0x342 |
| 4 rrE | face right at 802–830 | 2 (app+0x270 ? 0 : 0x3A) |
| 4 rrE | face left at base 886–908 and app+0x2aa | 1 (0) |
| 4 rrE | face left at 351–379 and app+0x2a8 | 6 (0) |
| 4 rrE | stop at d3: click "rrt ob sign" → RunRight | 2; "rrt rr sign" → runOff (continue) |
| 4 rrE | stop at d1, m6 bush open (click 0x271F or key 8) → RunLeft | 6 |
| 4 rrE | stop at d4, key 8 with gate closed (frame 0) → RunLeft | 6 *(as coded)* |
| 5 trE | movie end | 0 (0) |
| 6 wwE | movie end | HorseStop (stay) |
| 6 wwE | stopped at d0, space / continue | 7 (0) |
| 7 wwW | movie end | 4 (0x1AB) |

StartFrame 0x4378f0: (0→4) 0x447, (0→5) 0, (1→4) 0x408, (2→0) 0x164, (3→0) 0x38, (4→1) 0, (4→3) 0, (4→2) app+0x270 ? 0
: 0x3A, (4→6) 0, (5→0) 0, (6→7) 0, (7→4) 0x1AB, (7→7) 0; any other pair logs "UNHANDLED CASE" and returns 0.
Segment skips (set by SwitchTrail or InitTrail, cleared by LoadTrail): trail 0 entered at 0 → at frame 54 seek 166 (+0x33E);
trail 0 entered at 0x38 → at 100 seek 172 (+0x33C); trail 2 entered at 0 → at 57 seek 115 (+0x340).
Map jumps (GotoNodeById 0x439e20, the map toolbar): node id 7 tackroom → ClearStopProps + GotoNode("tackroom");
17 appleW → trail 0 @0x38; 18 cottageL → 1; 19 bridgeS → 2; 20 rabbitE → 4; 21 rainbowS → 3; 22 beachE → 5; 23 wishingL → 6.
Special start frames: app+0x342 → rabbitE @0x1CC; from 1/6/7 → rabbitE @0; bridge without app+0x270 → @0x3A.
Same trail → +0x40C restart (and replay deer). Then HorseRestart. If the current mission is 0x10, it is restored first.
Map marker (UpdateMapLocation 0x433c10, app+0x188 vt+0x14): spots 5–9 aaW, 15 cwL, 19–21 obS, 16/18 rfS, 10/11/13/14
rrE, 0–4 trE, 12 wishing well, chosen by frame against the decision-window table.

### Stops (HorseStop 0x432f30) and props per decision
Stops the voice, pauses the movie and gait, and computes the decision from the windows. Trail 3: window 0 uses anim
"decision1" unless mission 7; mission 12 after frame 90 → none. It shows `<p>t decision<N>` (or `generic0` if none)
and creates props:
trail 0 d0 aat rr sign (0x2712), d2 owl (0x2717, no rollover in mission 9), d3 cow cud + sandees cow (0x2718) +
cowchew; trail 1 none → cottageGrass; trail 2 d0 bell grass (frames 58–99: obt ggrass0, no decision), d1 bridge
rail, d2 obt dgrass2; trail 4 d0 mailbox (0x2713), d1 m6 bush (0x271F if app+0x2a8), d3 rrt rr sign + rrt ob sign
(0x2712), d4 m4 bush (+ m4 gate 0x271D if app+0x2aa), none with frame>1008 → d2; trail 6/7 d0 wishing well (0x2721,
frame 3 if bag item 0x17). The app+0x33C/0x33E arrival requests instead seek to fixed stop points (see save file).
Then it shows the toolbar and spawns critters (0x42fda0). It runs script "runOn" for a normal stop,
or LookLeft/Ambient when arriving, and caches the mission in +0x438. It calls 0x4412e0, 0x441ea0 and the OnStop
hook for the mission. Unless a voice is playing it calls SayStopComment and 0x430580. Finally it sets +0x4A4 = 1,
SetOutsidePalette 0x431180 (outside) and UpdateMapLocation.
Leaving a stop: space or message 0x32CC → mount if needed, then `runOff` (trail 5 d1: `runLeft`) → ScriptEnded →
HorseRestart. HorseRestart 0x432c90 clears the props and resumes the movie, or calls SwitchTrail when
+0x404 != +0x400.

### Methods
| addr | name | purpose |
|---|---|---|
| 0x431420 | TrailNode | ctor: toolbar "Out" buttons, perf globals from app+0x334/0x336, open omts, first-ride tutorial + **SaveGame**, mission init 0x430630, canvas, hi-mem test, field init, sound preload, InitTrail, gait sounds, initial anims, state HIDE_ALL, run "preload" |
| 0x431f60 | `scalar deleting destructor' | ~TrailNode + delete |
| 0x431f80 | Start | start gait sound unless an arrival stop is requested |
| 0x431fa0 | ~TrailNode | write app+0x30C, ClearStopProps, free movies (log Smack summary), sounds, palettes, anims/omts (if !app+0x338), restore tutorial mission, free preload list, ~HCToolbarNode |
| 0x432600 | MouseDown | ignored while riding; dispatch on clicked anim id (table below) |
| 0x432c90 | HorseRestart | leave a stop: clean props, resume or SwitchTrail, riding=1, gait |
| 0x432f30 | HorseStop | stop the horse at the current frame (see above) |
| 0x433c10 | UpdateMapLocation | set map marker spot |
| 0x433eb0 | ShowDecisionAnim | create `<p>t <name>` anim into +0x3D8 (logs "anim not found") |
| 0x433fd0 | DeleteDecisionAnim | delete +0x3D8 |
| 0x434000 | ClearStopProps | delete whichever stop props exist, the certificate, the pickup item (re-adds item), then 0x430250 and mission cleanup hooks |
| 0x4344c0 | SayStopComment | trail/decision-specific or random voice after stopping |
| 0x434610 | ShowCertificateName | draw player name (app+0x1B0) centred at x 320, y 270 on the certificate anim +0x6D8 |
| 0x434930 | CreateHiddenAnim | CreateAnimAtViewport + hide |
| 0x434950 | FindOrCreateHiddenAnim | FindAnim, otherwise create hidden |
| 0x434990 | LoadMountAnims | after first stop: bMountRun/<H>MountRun/fancy/ambient anims (click id 10000), states NO_STATE→HIDE_ALL |
| 0x434ad0 | LoadWalkAnims | after dismount: barbieWalkOutside, SaddleAndReigns or ambient sets |
| 0x434c70/80/90/a0 | dtor thunks | this −0xC/−0x18/−0x6C/−0x7C → 0x431f60 |
| 0x434cb0 | InitTrail | palettes, dirs, LoadTrail + LoadBackupMovies, bridge/rabbit start, working palette, fps, centre cursor |
| 0x434f60 | KeyDown | key codes 1 (Esc: stop + exit dialog), 6–9 (steer/speed flags; 8 at a stop → RunLeft), chars ' ' (stop / ride on), '$' (cheat bag item 0x18), 'A' (FeedApple), 'P' (debug horse swap), 'Y' (debug → yard) |
| 0x435400 | KeyUp | code 3 → HorseJump; clears key flags |
| 0x435510 | ListenToMessage | mouse up/down (-0x67/-0x66) → jump/stop/walk; 0x32CC continue; 0x32CD mission 8/9/10 hooks; 50000 hint + timer restart |
| 0x435980 | SpendTime | ride loop (above) |
| 0x436c50 | UpdateWaterSounds | falls/stream volume and pan from frame (AIL_quick_set_volume) |
| 0x436e80 | GetTrailFrame | logical trail frame from base, insert or backup movie |
| 0x436f10 | SetHorseFace | switch head overlay; when unchanged, check invalid-face table and slow down |
| 0x437280 | HorseJump | play "feedback", set jump flag |
| 0x4372f0 | UpdateObstacle | refusal/backup or jump arc at obstacles |
| 0x4377f0 | TrailEnded | end-of-trail handling (yard exit, loops, default next trail) |
| 0x4378f0 | StartFrame | start frame for (current, next) trail pair |
| 0x437a40 | SwitchTrail | current = next, reload base movie (keep fps), seek start frame, set skip flags; or restart the same trail |
| 0x437e50 | LoadTrail | create base movie + head overlays + deer, backups, ambient loop, music; logs load time |
| 0x438930 | LoadBackupMovies | create `back0..2` movies (+ reverse heads) for the current trail, or all of 0,3,4,5 if [0x8027a4] |
| 0x4390b0 | UnloadBackupMovies | delete them |
| 0x439160 | PlayInsertMovie | create +0x320 insert movie `<p> <name>` with head overlays and make it current |
| 0x4394e0 | LoadGaitSounds | build and preload 8×5 gait sound names |
| 0x4395f0 | ReleaseGaitSounds | release them |
| 0x439660 | SetGait | change gait index (restart loop if playing) |
| 0x4396b0 | PlayGaitSound | loop `+0x4BC[trail][gait]` |
| 0x439710 | StopGaitSound | stop it |
| 0x439760 | LoadButterflies | load 3 overlay sizes of a random species and direction |
| 0x439a00 | StartButterfly | show first size at the screen edge |
| 0x439aa0 | UpdateButterfly | move, grow and finish |
| 0x439ce0 | ToggleMount | click on horse (10000): dismount, mount, or walk to horse first |
| 0x439e20 | GotoNodeById | map jump: tackroom or in-node trail switch |
| 0x43a080 | ScriptCommand | horse-state/script-var commands (above) |
| 0x43aa40 | ScriptStarted | mount: stop HorseAmb; runOff: MOUNT_RUN |
| 0x43aab0 | ScriptEnded | script chaining, ride restart, tutorial end, WalkBack exit, mission ScriptEnded hooks, toolbar 0x41dc80 |
| 0x43b000 | ScriptPreempted | state fixes when Ambient/FeedApple/LookLeft/Mount/TMoment2/Walk are interrupted |
| 0x43b180 | AnimFrameCue | barbDirt footstep sound on walk-anim cues 0/3 (stop on 2/5) |
| 0x43b240 | SetHorseState | horse state machine |
| 0x43baa0 | ForceHideAnim | hide+disable by counters (static-like; also used by yard 0x43cda0 and mission code) |
| 0x43bb10 | ForceHideAnimByName | FindAnim + ForceHideAnim |
| 0x43bb30 | ForceShowAnim | inverse of ForceHideAnim (also used by mission code) |
| 0x43bba0 | WalkBarbieToMouse | walk Barbie towards +0x3FC in 120 px cycles, clamped to slots 0..4 |
| 0x43be80 | MountAndRide | mount (walking to the horse first if needed), then ride off |
| 0x43bf30 | WalkBarbieToHorse | walk Barbie back to slot 1, then mount |

MouseDown ids (anim+0x11C): 10000 horse/Barbie → ToggleMount; 0x2711 mini-game entrance (mission 4/3/7 handlers set
+0x6CC) → GotoNode(name[+0x6CC]) directly, or after the "WalkBack" script when dismounted on high memory;
0x2712 road sign; 0x2713 mailbox (mission 0 → 0x440bb0, else toggle with mailOpen/mailClos); 0x2714 pickup item
(missions 0/5/8/9 hooks, AddBagItem); 0x2715 certificate (db0197, pager delay 8000, mission 0xB → 0x11 + db0198);
0x2716 critter 0x4302a0; 0x2717 owl; 0x2718 cow; 0x2719/0x271A → 0x440bb0; 0x271B → 0x441550; 0x271C/0x271D → 0x4423d0;
0x271E → app+0x2F8=2, **GotoNode("finalpayoff")**; 0x271F m6 bush; 0x2720–0x2722 and 0x36B9 → 0x443710; 0x2723 → 0x444260;
0x2724 → 0x43f5a0; 0x2725 → 0x43fb60; 0x2726 → 0x444850; 0x2727/0x2728 → 0x444cb0; 0x2729 → 0x445080; 0x32CE → voice db02001;
anything else → toolbar 0x41c340.

### Transitions and saves
- Trail 0 frame 789 → GotoNodeFade("yard",16,1); prop 0x2711 / ScriptEnded("WalkBack") → GotoNode(nodeName[+0x6CC])
  (applesort / vinegame / carousel); prop 0x271E → GotoNode("finalpayoff"); map tackroom → GotoNode("tackroom");
  debug 'Y' → GotoNode("yard"); Esc → ShowExitDialog(1). Other trail-location changes stay inside the node.
- **SaveGame(app+0x1B0, [0x809ac0])** once, in the ctor, on the first trail ride (app+0x254 0→1).

### Mission hooks called (0x43e930–0x445820)
| when | mission (app+0x310) → function |
|---|---|
| ctor | 0x445340 after the first-ride save; 0x430630 (dispatcher) |
| dtor | 0xB → 0x440340; always 0x430730 (cleanup dispatcher), 0x4453b0 |
| HorseStop end | always 0x4412e0, 0x441ea0; 0→0x4406d0, 2→0x441700, 4→0x442960, 5→0x443130, 6→0x444080, 7→0x444380, 8→0x43ed00, 9→0x43f2d0, 10→0x43fad0, 11→0x43fec0, 12→0x4445d0, 13→0x444920, 14→0x444e80 |
| ClearStopProps (on +0x438) | always 0x4414c0, 0x4421c0; 0→0x440870, 2→0x441a70, 4→0x442af0, 5→0x4433d0, 6→0x4441e0, 7→0x444480, 8→0x43ee10, 9→0x43f460, 10→0x43fb30, 12→0x444740, 13→0x444b30, 14→0x444fe0 |
| SpendTime each new frame | 0→0x4409e0, 1→0x441570, 2→0x441dd0, 3→0x442290, 4→0x442c10, 5→0x443510, 6→0x4442d0, 7→0x4444f0, 8→0x43eeb0, 9→0x43f8e0, 10→0x43fd50, 12→0x4447c0, 13→0x444bd0, 14→0x445290; stopped + mission 5 → 0x443510 every tick |
| ScriptEnded | 0→0x441170, 2→0x441b60, 3→0x4427b0, 4→0x442de0, 5→0x443f50, 9→0x43f510, 14→0x445200 |
| ListenToMessage 0x32CD | 8→0x43f0b0, 9→0x43f6b0, 10→0x43fc20 |
| LoadTrail / TrailEnded | 0xB → C4LoadTrail 0x440040 (it tracks **lastTrailStarted at app+0x330** and C4 flags app+0x322..0x32E, outside the 0x150-byte record) |
| UpdateObstacle | 0x4457f0 (hint db0451) during the tutorial |
| MouseDown | see the id list (0x442d30, 0x4423d0, 0x444570, 0x440bb0, 0x443710, 0x43efc0, 0x43f5a0, 0x441550, 0x444260, 0x43fb60, 0x444850, 0x444cb0, 0x445080) |
Mission code calls back into TrailNode: HorseStop (0x445420), ShowDecisionAnim/DeleteDecisionAnim (0x441700, 0x442960,
0x444920), ShowCertificateName (0x43efc0, 0x43f5a0, 0x43fb60, 0x43fec0), GetTrailFrame (many), ForceHide/ShowAnim.

### Globals
[0x802790] max fps (14, low-end 12); [0x802794] head overlays on; [0x802798] music on; [0x80279c] ambient loops
on; [0x8027a0] ride voice hints; [0x8027a4] preload all backup movies; [0x8027a8] butterflies; [0x80a16c] /
[0x80a170] use `_L` trail / head movies; [0x80a174] no pan offsets; [0x80a40c] "in trail node" (ctor 1, dtor 0);
[0x80a438] global pause; [0x80a160] mystery mode (MysteryMusic, no critters or nags); [0x80a04c] one-shot bridge
arrival request (trail 2).

### Open questions
- app+0x258..0x274 are the mission counters M1..M8, app+0x2A8/0x2AA the wishing-well bush and cottage gate
  unlock flags (see the mission table below); app+0x2A2 is the cottage-quilt-viewed flag; app+0x336 is the yard
  debug mode ("sevanne").
- Key codes 1/3/6–9 come from the HC key mapping; their names here are inferred from behaviour.
- In KeyDown, d4 with key 8 also requests trail 6. This may be a copy-paste slip in the original.
- node+0x6C4 = 0x11 in the ctor is unexplained. The 7-dword block at +0x650 is unused here.

### TrailNode helper methods (0x42fda0–0x431380, TrailNode.cpp)

These TrailNode methods are compiled in TrailNode.cpp before the constructor:
| addr | name | purpose |
|---|---|---|
| 0x42fda0 | SpawnCritter | choose critter (global [0x80268c] 0..15, 0x10=none) from mission app+0x310, path +0x400 and segment +0x6B8; create anim from table 0x7F3770 (owl, critter_turtle, critter_rabbit, …, critter_redbirdfly) at +0x6A0, id 0x2716, rollover +0x6A4 |
| 0x430250 | RemoveCritter | delete critter anim/rollover |
| 0x4302a0 | CritterClicked | critter sound (turtle/newrabit/sparrow/…) + anim, maybe voice db03068 |
| 0x430580 | SayCritterComment | random voice db03068 / db3067 / per-critter table 0x7F37B0 |
| 0x430630 / 0x430730 | Load/ReleaseMissionVoices | Preload/ReleasePathVoices + per-mission (app+0x310 = 0..14) loader/releaser |
| 0x430830 / 0x430990 | Preload/ReleasePathVoices | keep/release db04115, db9050, db9051, db04032, db04040, db04033, db03066 (if app+0x26C), db04037, db04152, …, db04042 |
| 0x430ad0 | UpdatePathCommentary | per-frame Barbie comments by movie frame and segment (+0x7EC), counters +0x650.. ; thunder in mission 4; app+0x300 ⇒ 0x445420 |
| 0x431050 / 0x4310f0 | Open/CloseMissionResource | mission1/3/5/6.omt, cert1.omt by app+0x310 (stored in +0x6C4) |
| 0x431180 | SetOutsidePalette | apply palette +0x430 |
| 0x431200 | LoadPalette(name) (static) | 'DATA' chunk 0x300 bytes RGB (missing ⇒ all 0xFF) → palette object (0x476090) |
| 0x431380 | CopyUpperPaletteEntries(pal) | copy entries 0xC0..0xFF (or 0xFE..0xFF if [0x80a164]) from +0x434 |

### TrailNode mission, certificate and rescue code (0x43eca0–0x4457f0)

These are non-virtual TrailNode methods (this=node, size 0x7FC). Every caller is a TrailNode dispatcher, and the
functions use trail fields such as +0x400, +0x6B8 and +0x7EC. The class itself is described above:
ctor 0x431420(app, name, trailIdx), main vtable 0x7f39f8, secondary vtables 0x7f39f0/0x7f39e0/0x7f39d8/0x7f39c8,
dtor 0x431f60/0x431fa0. Derived per-trail ctors set node+0x7EC:
appleW 0x40a2f0=0, cottageL 0x40a450=1, bridgeS 0x40a500=2, rainbowS 0x40a660=3, rabbitE 0x40a5b0=4,
beachE 0x40a3a0=5, wishingL 0x40a710=6.

Dispatchers (TrailNode.cpp / TrailRide.cpp). Each one switches on the mission id **app+0x310**:

| dispatcher | when | per-mission method |
|---|---|---|
| 0x430630 / 0x430730 | load / release trail sounds | `X_LoadSounds` / `X_ReleaseSounds` (0x44c180 GetSound / 0x44c8b0 ReleaseSound of the mission's voices) |
| 0x432f30 HorseStop | horse stops. Sets +0x6B8 = stop index 0..4 from the per-trail frame windows, or 5 = "generic" stop, then always calls M2_SetupStop and M4_SetupStop | `X_SetupStop` |
| 0x434000 | stop cleanup. If a popup is still open it adds its item (+0x6C0) to the bag | `X_CleanupStop` (switch on +0x438, the mission id saved at the stop) |
| 0x435980 SpendTime | each new ride frame | `X_RideTick` |
| 0x432600 MouseDown | click ids 0x2711..0x2729, 0x36B9 | `X_MouseDown` |
| 0x435510 ListenToMessage | msg 0x32CD = toolbar camera | `Cn_CameraClick` |
| 0x43aab0 ScriptEnded | script end (`runOn`, `delay`, …) | `X_ScriptEnded` |

Generic machinery in 0x432600, used by the mission code:
- Popup click (0x2714) hides the popup and **AddBagItem(node+0x6C0)**.
- Certificate or badge click (0x2715) says db0197, deletes it, sets the pager delay to 8000 and runs `PagerDelay`.
  During mission 11 it also says db0198 and sets the mission to 0x11.
- 0x2711 lets the mission set +0x6CC and then goes to node name[+0x6CC] (0 applesort, 9 vinegame, 12 carousel) when
  the horse is at the stop. Otherwise it runs `WalkBack`.
- 0x271E sets app+0x2F8=2 and goes to `finalpayoff`.

Mission completion helper (HCToolbarNode::CheckAllMissionsDone 0x41e7e0): when all eight counters app+0x258..0x274 are nonzero
and app+0x2F8==0, it sets the mission to 15 (finale) and app+0x2F8=1, says DB04157 and DB04158, flashes the pager and
returns 1. Mission code skips its `PagerDelay` script in that case.

#### Key fields (node offsets)
| off | type | meaning |
|---|---|---|
| +0x0C8 | u16 | default cursor active (0 while a mission cursor is attached) |
| +0x0EC | anim* | current cursor anim |
| +0x0F4 | int | open toolbar panel (0x41c760(4) opens the saddlebag) |
| +0x174 | char* | current music name (trailMusic / mysteryMusic / gameMusic). Music changes go through 0x41d860 StopMusic, then PlayMusic |
| +0x17C | u16 | voice playing / camera busy |
| +0x2D4, +0x320, +0x324 | obj* | ride segment movies A and B and the current one. Frame = obj->+0xF0->+0x374, length = +0xC. 0x436e80 GetRideFrame combines them |
| +0x330 | u16 | horse riding (MouseDown ignored, M6 ride voices active) |
| +0x338 | u16 | mission interaction in progress (well handle or coin carried, branch game) |
| +0x3EC | u16 | set to 1 by the horseshoe badge (meaning unknown) |
| +0x400 | int | trail index (ctor arg; equals +0x7EC) |
| +0x404 | int | RunLeft/RunRight exit code (1 = through the m4 gate, 6 = past the m6 bush) *(inferred)* |
| +0x408 | int | trail entry frame (used by C4) |
| +0x444, +0x4A4 | u16 | horse-state flags (0x4A4≠0: horse standing at the stop) *(inferred)* |
| +0x64C | u16 | the stop has mission content |
| +0x6A8 / +0x6AC | anim*/ACRollover* | `owl` anim at appleW stop 2 (id 0x2717) |
| +0x6B8 | int | stop index (0..4 decision points, 5 = generic) |
| +0x6BC / +0x6C0 | anim* / int | open item popup (`…Out`, id 0x2714) / its bag item id |
| +0x6CC | int | node id to enter from a 0x2711 click |
| +0x6D0 / +0x6D4 | anim*/ACRollover* | `m4 cottage` or `m5 game` (id 0x2711) |
| +0x6D8, +0x6DC/+0x6E0 | anim*, obj* | certificate/badge anim (id 0x2715) and its name text (font holder, text element from 0x434610) |
| +0x6E4/+0x6F0 | anim*/ACRollover* | `mailbox` (id 0x2713) |
| +0x6E8/+0x6F4, +0x6EC/+0x6F8 | anim*/ACRollover* | `m1 knot` (0x2719), `m1 locket` (0x271A) |
| +0x6FE..+0x70A | u16 | M1 ride-voice "already said" flags |
| +0x70C | int | trail where the "wrong trail" hint was last given (7 = none) |
| +0x710/+0x714, +0x718 | anim*/ACRollover*, u16 | `coinFrog` (0x271B); db03003 already said |
| +0x71C, +0x720, +0x724, +0x728/+0x72A/+0x72C | anim*, u16, int, u16 | M3 rider anim, hint said, sighting count, seen at rabbitE / appleW / beachE |
| +0x730/+0x740, +0x734, +0x738/+0x73C/+0x744 | | `m4 gate` (0x271D) + hotspot, `cottageGrass`, `windMill` / `windMillOverlay` (0x271E) + hotspot |
| +0x748/+0x74A, +0x74C..+0x752 | u16 | M4 / M5 ride-voice flags |
| +0x754/+0x764 | anim*/ACRollover* | bush (`m4 bush` 0x271C or `m6 bush` 0x271F, created by HorseStop) |
| +0x758/+0x768 | anim*/ACRollover* | `wishing well` (0x2721) |
| +0x75C/+0x76C, +0x760/+0x770 | | `m6 hidden handle` (0x2720), `well squirrel` (0x2722) |
| +0x776..+0x77E | u16 | M6 ride-voice flags |
| +0x780 | HCMovie* | DrinkXxx.smk (all M6 clicks are ignored while it is non-null) |
| +0x784/+0x788, +0x78C | | `M7 bell`/`M7 nobell` (0x2723), ride-voice flag |
| +0x78E, +0x790/+0x794 | | M8 voice flag, `vines_shoe`/`vines_noshoe` (0x2711) |
| +0x798, +0x79C/+0x79E/+0x7A0, +0x7A2 | | C1 flower anim; daisy / tulip / rose hint flags; nature award done |
| +0x7A4, +0x7AC/+0x7AE/+0x7B0, +0x7B4 | | C2 animal anim (id 0x2724); owl / hawk / bluebird hint flags; unused hotspot |
| +0x7B8, +0x7BC | | `Certificate3` anim (0x2725); C3 voice flag |
| +0x7BE | u16 | a badge was shown at this stop |
| +0x7C0/+0x7C4, +0x7C8 | | `rescue2` (0x2726) + hotspot; voice flag |
| +0x7CC/+0x7D0, +0x7D4, +0x7D8/+0x7DA | | `rescue1` bird (0x2727) + hotspot; `rescue1`/`nest` (0x2728); voice flags |
| +0x7DC/+0x7E0, +0x7E4, +0x7E8 | | `r3branch` (0x2729) + hotspot; `r3clearbg`; voice flag |
| +0x7EC | int | this trail's id |
| +0x7F0 | int | trail on which mission content was set up (−1 = none) |
| +0x7F4 | int | pager delay in ms for the `PagerDelay` script (8000/9000/13000/0x4330/30000) |

#### Mission progression (app+0x310 = mission, app+0x314 = trail where it happens, 7 = any)
Missions are handed out by the bulletin board (0x40c110 region). It sets 0x310/0x314 and adds the mission note
(bag item = mission+1). Certificate missions add items 9..12, and rescues come from pager calls (0x406970 region).
"Clear" means app+0x310=0x11 and app+0x314=7. Counters are u32 and only ever incremented.

| id | mission | where (trail/stop) | counter | steps → effects |
|---|---|---|---|---|
| 0 | **M1** locket | rabbitE(4)/0 mailbox → appleW(0)/2 knot → beachE(5)/1 locket | app+0x258 | (1) Mailbox click: open (`mailOpen`). If the bag has neither item 13 nor 14 → sequence 2 with db04090. Clicking sequence 2 plays `clue`, says db04091, db0013, db04095, db04092, shows popup `locketNote1Out` (item **13**) and sets app+0x314=0. (2) At appleW stop 2, `m1 knot` appears only if HasBagItem(13). Clicking the `owl` (0x2717) makes the knot clickable (db04094). The knot gives `locketNote2Out` (item **14**) with db04004 and sets app+0x314=5. (3) `m1 locket` at beachE stop 1: plays `locketOpen` and the `musicbox` loop, says db04164 (app+0x2A0==0) or db04165, shows `locketOut` (item **15**), increments 0x258 and **saves**. Closing that popup plays `locketClose` and db9023 and sets the pager delay to 8000; the mission clears if still 0. Quirk: leaving the locket stop with the locket unopened also clears the mission (0x440870). |
| 1 | **M2** coin frog | bridgeS(2)/1 | 0x25C (set by leapfrog 0x41fcd0) | `coinFrog` (0x271B) runs script `bridgeFrog` (→ leapfrog game *(inferred)*). Leapfrog increments 0x25C, saves, gives coin item **24**, sets app+0x314=7 and global 0x80a168=1. Back at the stop: random DB9023/db9022, pager delay 9000, clear (unless the finale started), and the bag opens. Without the reward the frog says DB04118. Ride: db03003 and `Croak` every 4 s near frames 0x65..0x9A; wrong-trail hint db04117 after 25 ticks. |
| 2 | **M3** mystery rider | any trail: appleW/5 frames 0x295..0x2FC (Christie or Teresa by horse, `m3 aa …`), rabbitE/5 frames 0x27F..0x2CF (`m3 rr deer`), beachE/1 (`m3 tr mystery`, mysteryMusic) | 0x260 | Loads background `m3 bkgd` and the overlay with the rider hidden. On `runOn` → RunDelay(100), then on `delay` the rider is shown with voices dc0484+db04011a / dt0485+db04012 (+db04011b), or db04013 for the deer. The 3rd sighting increments 0x260, clears, **saves** and sets the pager delay to 13000. The beachE horse completes it at once (0x41e8f0 comment, db9023, **save**, pager delay 0x4330). Other appleW stops say db03009. |
| 3 | **M4** cottage | rabbitE(4)/4 bush+gate → cottageL(1)/0 | 0x264 (set by carousel ctor 0x40f860) | Click `m4 bush` (`branch`, DB04125). Click `m4 gate` (`gateopen`, `key`, DB04126) → app+0x2AA=1, app+0x314=1, **save**. At cottageL: `m4 cottage` (0x2711 → +0x6CC=12 → **carousel**), `cottageGrass`, `windMill`. Voices: DB9022 after the carousel (global 0x809afc), DB0078+DB9048 the first time, DB0519 later. When app+0x2F8==1 (finale) the windmill and overlay get id 0x271E (DB04159), and clicking sets app+0x2F8=2 and goes to **finalpayoff**. Ride: db04123 (>0x35C), db0074 (>900), db0075 on `runOn` at the gate. |
| 4 | **M5** roses | appleW(0)/1 | 0x268 (set by applesort dtor 0x401bd0, which also sets global 0x80a160) | Background `m5 bkgd`, `m5 game` (0x2711 → +0x6CC=0 → **applesort**), `m5 roses`. After a won applesort: `m5 overlay` plus `<H>LookLeft`, then scripts ExcitedLook / LookLeft / M5Payoff and voices db04018 / db04154 / db0116, thunder2, db04019. When both payoff scripts have ended, 0x41df10 offers a rescue. Ride voices db0100/db0104, db04149, db0106, db03014; wrong-trail hint db04148. |
| 5 | **M6** wishing well | rabbitE(4)/1 bush+rabbit → wishingL(6)/0 well | 0x26C | Click `m6 bush` → app+0x2A8=1, app+0x314=6, random db0482/db0135/db0134 (`runOn` then plays db04054 and script `M6_Rabbit`). At the well (gameMusic; a bottle item 23 still in the bag is removed): find `m6 hidden handle` (random sequence 0..2, db0138), carry it as a cursor to `m6 hot spot` (`handle`, db04059, well sequence 1). Clicking the well plays `clue`/`well` and db04061 (sequence 2). Clicking again goes to sequence 3 with db04062 (+db0142/db0143), popup `bottleOut` (item **23**), increments 0x26C, sets app+0x314=7 and **saves**. Clicking sequence 3 plays **DrinkXxx.smk** with `drink`. The bag coin (click id 0x36B9) removes item 24 and becomes `m6 coin cursor`; throwing it plays `m6 coin toss`, db04155 and a random wish line db02010..13. Closing the bottle popup says db04063 then db03024, or db02024 + db03023/db02023 if the coin is still held, with pager delay 30000. |
| 6 | **M7** bell | bridgeS(2)/0 | 0x270 (set by bellgame 0x40b7b0) | `M7 nobell` (or `M7 bell` once won) with id 0x2723 → **bellgame**. DB9018 the first time; random DB9022/DB9023 after a win (global 0x809ad4 = back from bellgame). Ride: db0147N / db02025. |
| 7 | **M8** vines | rainbowS(3)/0 | 0x274 (set in VineGame) | `vines_shoe` / `vines_noshoe` (0x2711 → +0x6CC=9 → **vinegame**). Ride: db0176. |
| 8 | **C1** nature certificate | any: appleW frames 0x221..0x28A `aat roses` (db0209), bridgeS 0x28..0x87 or 0xAA..0x118 `obt tulips` (db0207), rainbowS 0x69..0x15E `rft daisies` (db0208) | 0x278; photo flags 0x2AE rose / 0x2B0 tulip / 0x2B2 daisy | The camera (msg 0x32CD) at a flower shows a `c1 …PicOut` popup (items **16/17/18**) and sets the flag. When the popup is closed with all 3 flags set: increments 0x278, clears, **saves**, shows `certificatewon_nature` with the name, says db0211. Hints: db0203 per flower trail; db9027 on trails without flowers. |
| 9 | **C2** wildlife certificate | appleW/2 `owl`, rabbitE/5 frames 0xD3..0x149 `c2hawkanim` (db0222), beachE/5 frames 0x97..0x135 `c2bbirdanim` (db0230) | 0x27C; flags 0x2B4 owl / 0x2B8 hawk / 0x2B6 bluebird | The camera while the animal is at sequence 0 shows the `c2 …picOut` popup (items **19/20/21**). Award (popup closed or animal clicked, all 3 flags): increments 0x27C, clears, **saves**, shows `certificatewon_wildlife`, says db0234, db0197, db0198. |
| 10 | **C3** aid certificate | rainbowS(3)/0 | 0x280 | Arrival: spause DB0192 spause DB04085. The camera shows `Certificate3` (id 0x2725, z=1000), increments 0x280, **saves**, gives the **rainbow photo item 22** and says DB0195. Clicking the certificate says DB04086, clears, **saves** and shows `certificatewon_aid`. Ride: db04084. |
| 11 | **C4** trail certificate | ride every trail | 0x284; flags 0x322..0x32E, state 0x330 (outside the record) | C4LoadTrail and C4UnloadTrail track which trail was started (0x330 code) and set u16 flags when a trail is ridden to its end (within 25 frames) or into the next trail. Groups: appleW (0x322 and 0x324), rainbowS 0x326, rabbitE (0x328 and 0x32A), bridgeS 0x32C, beachE 0x32E. Each new group count says db0241, db0248, db0242, db0243, db0245. At 5 groups: increments 0x284, sets **app+0x320=1**, **saves**. `certificatewon_trail` appears at the next stop or in the yard; clicking it clears the mission. |
| 12 | **R2** horseshoe badge | rainbowS/5, frames 0x5B..0x9F | 0x288 | `rescue2` (0x2726), DB0253. Click: DB0255, increments 0x288, clears, **saves**, shows `badgeswon_horseshoe`. Ride: db0252. |
| 13 | **R1** bird badge | appleW/5, frames 0xEF..0x12B | 0x28C | Background `rescue1Background`, DB0270, bird (`rescue1`, 0x2727, z=175) and `rescue1`/`nest` (0x2728, z=174). Click the bird: DB0272 and the cursor becomes `rescue1` (the old cursor name is kept at 0x80a2a0). Click the nest: sequence 2, DB0273, increments 0x28C, clears, **saves**, shows `badgeswon_bird`. Ride: DB0269, DB9064. |
| 14 | **R3** branches badge | rabbitE/5, frames 0x211..0x25A | 0x290 | `r3branch` (0x2729) + `r3clearbg`. Each click plays `branch` and advances both sequences. The 3rd click says db0289, increments 0x290, clears, **saves** and shows `badgeswon_branches`. db0286, plus db0287 on `delay`. |

Lesson voices (app+0x300 tutorial ride on rabbitE, variant by app+0x1A8 from the pair table at 0x804b40):
db0447/0442, db0448/0443, db0451/0445, db02036/0441, db0452/0446, db0449/0444 at fixed frames, then db0453.
At frame >0x2D5 it forces HorseStop and sets +0x6C8.

#### Methods
| addr | name | purpose |
|---|---|---|
| 0x43eca0 / 0x43ecd0 | C1_LoadSounds / C1_ReleaseSounds | db0203, db9027 |
| 0x43ed00 | C1_SetupStop | Creates the flower anim in its frame window and says the flower voice. Hides `bridge rail` on bridgeS. |
| 0x43ee10 | C1_CleanupStop | Deletes the flower anim and `picUpClose`. Re-arms hints for photos not yet taken. |
| 0x43eeb0 | C1_RideTick | db0203 hint per flower trail. db9027 after 100 ticks on other trails (global 0x80a268). |
| 0x43efc0 | C1_MouseDown | 0x2714 with all 3 photos → nature certificate, save |
| 0x43f0b0 | C1_CameraClick | photo popup and flags; can increment 0x278 |
| 0x43f2d0 | C2_SetupStop | Sets up the owl, hawk or bluebird as the photo target. |
| 0x43f460 | C2_CleanupStop | Deletes the animal and popup. Re-arms hints. |
| 0x43f510 | C2_ScriptEnded | beachE: `runOn` → RunDelay(500); `delay` → show bluebird |
| 0x43f5a0 | C2_MouseDown | wildlife award, save |
| 0x43f6b0 | C2_CameraClick | photo popups and flags |
| 0x43f8e0 | C2_RideTick | `owl`+db0223, `hawk`+db0218, `bluebird`+db0227 cues |
| 0x43f9d0 / 0x43fa50 | C2_LoadSounds / C2_ReleaseSounds | |
| 0x43fad0 | C3_SetupStop | arrival voices at rainbowS stop 0 |
| 0x43fb30 | C3_CleanupStop | deletes `Certificate3` |
| 0x43fb60 | C3_MouseDown | certificate click → aid award, save |
| 0x43fc20 | C3_CameraClick | Rainbow photo → Certificate3, 0x280++, save, item 22. The 0x2714/0x2725 branches are unreachable because the caller always passes 0x32CD. |
| 0x43fd50 | C3_RideTick | db04084 |
| 0x43fdb0 / 0x43fdd0 | C3_LoadSounds / C3_ReleaseSounds | |
| 0x43fdf0 / 0x43fe60 | C4_LoadSounds / C4_ReleaseSounds | db0241 db0242 db0243 db0245 db0248 |
| 0x43fec0 | C4_SetupStop | Shows the pending trail certificate (app+0x320). |
| 0x43ff10 | C4_UpdateProgress | Counts the ridden groups, says the progress voice when it changes (global 0x80a274), awards at 5 and saves. |
| 0x440040 | C4LoadTrail(bool atStart) | marks finished groups on a trail change and records lastTrailStarted |
| 0x440340 | C4UnloadTrail | called from the TrailNode dtor; marks the group done if within 25 frames of the end and resets 0x330 to 7 |
| 0x440580 / 0x440630 | M1_LoadSounds / M1_ReleaseSounds | The release list names db0015 where the load list has db0017 (original typo). |
| 0x4406d0 | M1_SetupStop | creates the knot (if item 13 is held) or the locket |
| 0x440870 | M1_CleanupStop | removes the knot or locket (quirk: clears the mission). Re-arms voice flags from bag items 13/14/15. |
| 0x4409e0 | M1_RideTick | ride voices and wrong-trail hint db04092 (global 0x80a278) |
| 0x440bb0 | M1_MouseDown | mailbox, owl, knot, locket and popup close |
| 0x441170 | M1_ScriptEnded | `runOn`/`delay` prompts db04093, db0013, db0010 |
| 0x4412e0 | M2_SetupStop | coin frog and reward |
| 0x4414c0 | M2_CleanupStop | |
| 0x441550 | M2_MouseDown | 0x271B → script `bridgeFrog` |
| 0x441570 | M2_RideTick | |
| 0x441670 / 0x4416c0 | M2_LoadSounds / M2_ReleaseSounds | |
| 0x441700 | M3_SetupStop | |
| 0x441a70 | M3_CleanupStop | |
| 0x441b60 | M3_ScriptEnded | sightings and completion |
| 0x441dd0 | M3_RideTick | db04098 |
| 0x441e60 / 0x441e80 | M3_LoadSounds / M3_ReleaseSounds | |
| 0x441ea0 | M4_SetupStop | cottage and windmill, or gate |
| 0x4421c0 | M4_CleanupStop | |
| 0x442290 | M4_RideTick | |
| 0x442320 / 0x442380 | M4_LoadSounds / M4_ReleaseSounds | |
| 0x4423d0 | M4_MouseDown | cottage → carousel, bush, gate (save), windmill → finalpayoff |
| 0x4427b0 | M4_ScriptEnded | db0075 |
| 0x442810 / 0x4428c0 | M5_LoadSounds / M5_ReleaseSounds | includes `thunder` and `thunder2` |
| 0x442960 | M5_SetupStop | |
| 0x442af0 | M5_CleanupStop | |
| 0x442c10 | M5_RideTick | |
| 0x442d30 | M5_MouseDown | Cancels the payoff, resets the music and sets +0x6CC=0 (applesort). |
| 0x442de0 | M5_ScriptEnded | payoff chain (global 0x80a294 counts finished scripts) |
| 0x443000 / 0x4430a0 | M6_LoadSounds / M6_ReleaseSounds | The release list misspells three names (db4055, db4056, db2006). |
| 0x443130 | M6_SetupStop | |
| 0x4433d0 | M6_CleanupStop | |
| 0x443510 | M6_RideTick | Riding voices db02022, db04053, db0132, db0572, db02006/db04055/db04056. Also called from SpendTime while standing. |
| 0x443710 | M6_MouseDown | bush, handle, well, coin, squirrel, bottle (save), drink movie |
| 0x443f50 | M6_ScriptEnded | |
| 0x444080 | M7_SetupStop | |
| 0x4441e0 | M7_CleanupStop | |
| 0x444260 | M7_MouseDown | → bellgame |
| 0x4442d0 | M7_RideTick | |
| 0x444340 / 0x444360 | M7_LoadSounds / M7_ReleaseSounds | |
| 0x444380 | M8_SetupStop | |
| 0x444480 | M8_CleanupStop | |
| 0x4444f0 | M8_RideTick | |
| 0x444570 | M8_MouseDown | +0x6CC=9 (vinegame) |
| 0x444590 / 0x4445b0 | M8_LoadSounds / M8_ReleaseSounds | |
| 0x4445d0 | R2_SetupStop | |
| 0x444740 | R2_CleanupStop | |
| 0x4447c0 | R2_RideTick | |
| 0x444810 / 0x444830 | R2_LoadSounds / R2_ReleaseSounds | |
| 0x444850 | R2_MouseDown | badge, save |
| 0x444920 | R1_SetupStop | |
| 0x444b30 | R1_CleanupStop | |
| 0x444bd0 | R1_RideTick | |
| 0x444c50 / 0x444c80 | R1_LoadSounds / R1_ReleaseSounds | |
| 0x444cb0 | R1_MouseDown | badge, save |
| 0x444e80 | R3_SetupStop | |
| 0x444fe0 | R3_CleanupStop | |
| 0x445080 | R3_MouseDown | badge, save |
| 0x445200 | R3_ScriptEnded | |
| 0x445290 | R3_RideTick | |
| 0x445300 / 0x445320 | R3_LoadSounds / R3_ReleaseSounds | |
| 0x445340 / 0x4453b0 | LessonLoadSounds / LessonReleaseSounds | called from the TrailNode ctor / dtor |
| 0x445420 | LessonRideTick | rabbitE tutorial ride voices (from 0x430ad0 when app+0x300≠0) |
| 0x4457f0 | LessonIdleHint | db0451/db0445 every 30 s while idle (from 0x4372f0) |

Every method above is `TrailNode::`.

#### Transitions and saves in the mission code
- Transitions:
  - → `finalpayoff` (windmill, app+0x2F8=2).
  - → `bellgame` (M7).
  - via +0x6CC → `carousel` (cottage), `applesort` (m5 game), `vinegame` (vines).
  - Script `bridgeFrog` (M2).
- Save (0x401df0 with app+0x1B0 and slot 0x809ac0) in 0x43efc0, 0x43f5a0, 0x43fb60, 0x43fc20, 0x43ff10, 0x440bb0,
  0x441b60 (×2), 0x4423d0, 0x443710, 0x444850, 0x444cb0, 0x445080.
  In the other two classes: VineGame 0x43c930. Yard: none.
- AddBagItem called directly: 0 (VineGame) and 22 (C3). Popup items 13–21 and 23 are added when the popup is
  clicked away (0x432600) or when the stop is cleaned up (0x434000).
- RemoveBagItem: 23 (M6 setup, if held) and 24 (coin used at the well).

#### Open questions
- What do the voice-variant flags app+0x29C, 0x29E and 0x2A0 mean? They might be gender or the friend met.
- Is the M1 quirk intended? Leaving the locket stop without opening the locket clears mission 0 without
  incrementing app+0x258.
- app+0x310, 0x314 and 0x320..0x330 lie outside the 0x150-byte record: they are not saved. Godiva::Start sets
  app+0x310 = 0x11 (no mission), app+0x314 = 7 and app+0x320..0x32E = 0, so an active mission, the C4 trail
  flags and lastTrailStarted are lost when the game is quit (savegame.md).
- Who runs script `bridgeFrog` → leapfrog, and how does the yard intro (`Intro` script) end? Both are in
  script data, not code.

### The seven trail-location classes

Each class is only a constructor/destructor wrapper around TrailNode. Factory size 0x7FC for all.
Constructor: `TrailNode(app, name, trailIndex)`, then the class's own vtables, then **app+0x338 (u16) = 0** and
**node+0x7EC = trailIndex**. TrailNode itself already stores trailIndex at +0x400/+0x404 and sets +0x7EC = table_0x8027b0[idx]
(an identity map 0..6, with 7→6), so the derived store is redundant. TrailNode does the asset loading (trails.omt,
trlAnims.omt(.cache), the trailPal/… palette omt picked by app+0x1C0); see the TrailNode section above.
The appleW ctor addresses the app through the ctor argument; the others go through node+0x1B8 (same object).

| node name | class (factory debug name) | trailIndex | ctor | sdd | dtor | main vtbl | +0xC | +0x18 | +0x6C | +0x7C | thunks (−0xC,−0x18,−0x6C,−0x7C) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| appleW | AppleWest ("Apple West") | 0 | 0x40a2f0 | 0x40a350 | 0x40a370 | 0x7f03a0 | 0x7f0398 | 0x7f0388 | 0x7f0380 | 0x7f0370 | 0x40a800/810/820/830 |
| cottageL | CottageWayLoop ("CottageWayLoop") | 1 | 0x40a450 | 0x40a4b0 | 0x40a4d0 | 0x7f05c0 | 0x7f05b8 | 0x7f05a8 | 0x7f05a0 | 0x7f0590 | 0x40a880/890/8a0/8b0 |
| bridgeS | OldBridgePath ("OldBridgePath") | 2 | 0x40a500 | 0x40a560 | 0x40a580 | 0x7f06d0 | 0x7f06c8 | 0x7f06b8 | 0x7f06b0 | 0x7f06a0 | 0x40a8c0/8d0/8e0/8f0 |
| rainbowS | RainbowFalls ("RainbowFalls") | 3 | 0x40a660 | 0x40a6c0 | 0x40a6e0 | 0x7f08f0 | 0x7f08e8 | 0x7f08d8 | 0x7f08d0 | 0x7f08c0 | 0x40a940/950/960/970 |
| rabbitE | RabbitEast ("Rabbit East") | 4 | 0x40a5b0 | 0x40a610 | 0x40a630 | 0x7f07e0 | 0x7f07d8 | 0x7f07c8 | 0x7f07c0 | 0x7f07b0 | 0x40a900/910/920/930 |
| beachE | BeachEast ("Beach East") | 5 | 0x40a3a0 | 0x40a400 | 0x40a420 | 0x7f04b0 | 0x7f04a8 | 0x7f0498 | 0x7f0490 | 0x7f0480 | 0x40a840/850/860/870 |
| wishingL | WishingWayLoop ("WishingWayLoop") | 6 | 0x40a710 | 0x40a770 | 0x40a790 | 0x7f0a00 | 0x7f09f8 | 0x7f09e8 | 0x7f09e0 | 0x7f09d0 | 0x40a7c0/7d0/7e0/7f0 |

Vtables: main has 56 slots (50 HCNode + 5 toolbar + TrailNode slot +0xDC); only slot 0 (sdd) is overridden. In the
secondary vtables only the destructor slots are thunks; everything else is TrailNode's (SpendTime 0x435980, ListenToMessage 0x435510).
dtor = restore own vtables, then TrailNode dtor 0x431fa0. Note that the factory string compare for "rainbowS" appears twice (dead duplicate).

## AppleSort — factory "applesort" (mission M5)

The apple-sorting mini-game (Pachinko style):
- A red or a green apple rolls in at the top and falls through 10 rows of pegs.
- 8 clickable flippers (each toggles between sequence 0 and 1) steer the apple left or right.
- Red apples must land in the right bucket (lane 4) and green apples in the left bucket (lane 0).
- Sorting 3 of each wins.

Class name confirmed by the string "AppleSort::AddApple()".
- Base: HCToolbarNode (ctor 0x41b690).
- ctor 0x4015e0(app, arg2). Real dtor 0x401bd0. Scalar deleting dtor 0x401bb0.
- Size 0x270.
- Vtables. The main vtable has 55 slots; only the overridden ones are listed. Slots +0x9C..+0xB8 are the shared
  empty hooks 0x401b80/0x401b90/0x401ba0/0x40bd60, the same as the base.

| vtable | at | slots |
|---|---|---|
| 0x7f0118 | +0x00 | **+0x00 0x401bb0 dtor**, **+0x54 0x4021b0 MouseDown**, **+0x6C 0x403d70 KeyDown**, **+0x80 0x403cd0 SoundEnded**, **+0x90 0x403ca0 ScriptEnded**, **+0x98 0x403a70 ScriptCommand** |
| 0x7f0110 | +0x0C | +0 0x403ed0 (dtor, this−0xC), **+4 0x402150 SpendTime** |
| 0x7f0100 | +0x18 | **+0 0x402260 ListenToMessage**, +4 0x403ee0 (dtor, this−0x18), +8 0x45bd80 |
| 0x7f00f8 | +0x6C | +0 0x403ef0 (dtor, this−0x6C), +4 0x403f00 (shared adjustor → 0x45c880) |
| 0x7f00e8 | +0x7C | +0 0x403f10 (dtor, this−0x7C), 0x45c580, 0x45c510, 0x45c470 (broadcaster, inherited) |

**Assets:**
- Opens `applesort.omt.cache` (OpenResourceFile with prepend=1, cache=1) and `GameBarb.omt` (closed in the dtor).
- Runs script `GameAmb` (ambient; stopped in the dtor through 0x455c10). The `Flippers` script is run per apple.
  The script `initFlippers` command is handled by ScriptCommand.
- Ambient loop sound `wind`; its name is stored at node+0x178.
- Cursor `cursorOut`. Toolbar buttons tbQuitOut(0) tbPagerOut(1) tbCameraOut(2) tbMapOut(3) tbBagOut(4)
  tbHelpOut(5) tbHomeOut(6).
- Anims:
  - `apple_buckets` (id 0x2719, with an ACRollover).
  - `red_apple2`, `green_apple`, `red_bounce`, `green_bounce` (the bounce anims start force-hidden).
  - `flipper1`..`flipper8` (ids 10001..10008, each with an ACRollover).
  - 3 bucket apples per colour: green_apple instances "green apple A/B/C" at x=−119 and red_apple2 instances at
    x=151, offset dx 0/−10/+10, y 321/319/317, hidden.
  - `leaf`.
  - Easter-egg apple "green apple Z".
- Sounds: lever, apple (rolling loop), bonk, pop, boing, right, wrong, win, thunder, thunder2, whinney2.
- Voices: db0108, db03015 (intro), db0109, db0112 (win), db0110/db0111/db04150 (miss), db03016/db03017 (hints),
  db03018 (help), db04151 (exit).
- The ctor deletes the anims flipper0..8, red_apple and the others in the dtor.

**Key fields** (node offsets):

| off | type | meaning |
|---|---|---|
| +0x1C0 | u16 | game started (set by clicking apple_buckets) |
| +0x1C2 | u16 | game won |
| +0x1C4 | int | apples in play (0/1) |
| +0x1C8 | int | row state of the falling apple (0..9) |
| +0x1CC | HC2DAnim* | current falling apple (= +0x1F0 or +0x1F4) |
| +0x1D0..+0x1E4 | HC2DAnim*[2][3] | bucket apples [0 = green, 1 = red][count] |
| +0x1E8 / +0x1EC | int | green / red apples sorted (0..3) |
| +0x1F0 / +0x1F4 | HC2DAnim* | red_apple2 / green_apple (falling apples) |
| +0x1F8 | HC2DAnim* | easter-egg apple (cheat), deleted when y > 165 |
| +0x1FC / +0x200 | HC2DAnim* | green_bounce / red_bounce |
| +0x208..+0x224 | HC2DAnim*[8] | flipper1..8 (`node+0x204+4*k`) |
| +0x22C..+0x248 | ACRollover*[8] | flipper rollovers |
| +0x24C | ACRollover* | bucket rollover, deleted at game start |
| +0x250 | HC2DAnim* | leaf |
| +0x254 | int | streak (>0 right in a row, <0 wrong in a row) |
| +0x258 | float | sideways speed sx = 72 + 4.8·W |
| +0x25C | float | downward speed while deflected sy = 30 + 2·W |
| +0x260 | float | straight fall speed sf = 78 + 5.2·W (W = app+0x268 wins) |
| +0x264 | u16 | voice hints on (1 at start) |
| +0x266 | u16 | a flipper has been used |
| +0x268 | char[3] | cheat key buffer; +0x26C int count |

Static floats (set by the initializers at 0x401400–0x401500): minimum sx 43.2 (0x809948), sy 18 (0x809938),
sf 46.8 (0x80993c). Per-win increments: 4.8 (0x809940), 2.0 (0x809944), 5.2 (0x80994c).

**Gameplay:**
- **Ctor:**
  - Sets app+0x310 = 4 and app+0x314 = 0 if app+0x310 != 4.
  - Sets node+0xC4 = 1, node+0xCE = 1, node+0xD0 = 0x11 (appleW), node+0x108 = 1 (palette "Out").
  - Restarts the ambient timer at app+0x19C.
  - Voices: if wins (app+0x268) == 0, plays "pause", db0108, pause, pause, db03015. Otherwise plays
    DelayVoice(db0108, 750).
- **MouseDown** 0x4021b0. The id is read from the clicked element:
  - 0x2719 (buckets): if not started, starts the game and deletes the bucket rollover. If already started, does
    nothing.
  - 10001..10008: ToggleFlipper (0x4020e0) plays "lever", sets +0x266 = 1 and toggles the flipper sequence.
  - 0x32CE (the help button id): PlayVoice(db03018), then the toolbar handler.
  - Other ids: toolbar MouseDown 0x41c340.
- **SpendTime** 0x402150. While started and not won:
  - If no apple is in play and script "Flippers" is not running, runs "Flippers".
  - If an apple is in play, calls UpdateApple.
- **ScriptCommand(script, "initFlippers")** 0x403a70: picks 4 distinct random flipper numbers 1..8. For each it
  sets the script vars `flipper1..4` = "flipperN" (0x455cb0). If that flipper's sequence is 0, it sets `seqK` = 1.0
  (`seq1`..`seq4`). Finally it sets `delay` = 250.0.
- **ScriptEnded("Flippers")** → AddApple (0x4023a0). AddApple runs only if no apple is in play:
  - Colour: if either colour is complete (count 3), the incomplete colour is picked with probability 3/4.
    Otherwise the colour is random 50/50.
  - Red starts at (36, 0, 75) with vx = −sx. Green starts at (0, 0, 75) with vx = +sx.
  - Sets sequence 1, plays "pop" and increments apples-in-play.
  - If hints are on, plays NoFlap db03016 (red) or db03017 (green).
- **UpdateApple** 0x402520 (one apple only):
  - Lanes (SnapToLane 0x4034f0: window → snapped x): 0: (−122,−116) → −119; 1: (−55,−50) → −53;
    2: (15,21) → 18 (also matches at y == 0 when the apple has rolled past 15 or 21); 3: (81,86) → 84;
    4: (148,153) → 151.
  - Rows (IsAtRow 0x403710, y window; SnapToRow 0x403930 snaps y):

    | row | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
    |---|---|---|---|---|---|---|---|---|---|---|
    | y window | 40–50 | 67–77 | 96–106 | 126–136 | 154–164 | 183–193 | 212–222 | 239–249 | 268–278 | 298–308 |
    | snapped y | 45 | 72 | 101 | 131 | 159 | 188 | 217 | 244 | 273 | 303 |
  - Start: while y == 0 the apple rolls sideways. On reaching lane 2: plays the "apple" loop, sets velocity
    (0, sf) (↓), state = 0.
  - Moves: "→R" = (+sx, sy), "→L" = (−sx, sy), "↓" = (0, sf). "bonk" plays on deflection. In some branches it
    plays only when the direction actually changes; the code compares anim vx·1000 with ±sx.
  - State table. Fk = seq of flipper k (0 or 1). The flipper positions are implied: F1 r1/l1, F2 r1/l3, F3 r2/l2,
    F4 r3/l1, F5 r3/l3, F6 r4/l2, F7 r4/l0, F8 r6/l2.

    | state | row | lane | action |
    |---|---|---|---|
    | 0 | 0 | any | bonk, random →R or →L; state 1 |
    | 1 | 1 | 1 | F1 = 0: bonk, →R; F1 ≠ 0: keep moving; state 2 |
    | 1 | 1 | 3 | F2 = 0: ↓, state 3; F2 ≠ 0: bonk, →L, state 2 |
    | 2 | 2 | 0 | bonk, →R; state 3 |
    | 2 | 2 | 2 | F3 = 0: bonk, →R, state 3; F3 ≠ 0: ↓, state 4 |
    | 3 | 3 | 1 | F4 = 0: →R; F4 ≠ 0: →L; state 4 |
    | 3 | 3 | 3 | F5 = 0: →R; F5 ≠ 0: →L; state 4 |
    | 4 | 4 | 0 | F7 = 0: bonk, →R, state 5; F7 ≠ 0: ↓, state 6 |
    | 4 | 4 | 2 | F6 = 0: →R; F6 ≠ 0: →L; state 5 |
    | 4 | 4 | 4 | →L, bonk; state 5 |
    | 5 | 5 | 1 | →R; state 6 |
    | 5 | 5 | 3 | ↓; state 7 |
    | 6 | 6 | 0 | →R, bonk; state 7 |
    | 6 | 6 | 2 | F8 = 0: →R, state 8; F8 ≠ 0: →L, state 7 |
    | 7 | 7 | 1 | →L; state 8 |
    | 7 | 7 | 3 | →R; state 8 |
    | 8 | 8 | 0 or 4 | bonk, ↓; state 9 |
  - y > 315: stops the "apple" sound. The apple is correct if it is red in lane 4 or green in lane 0.
    - **Correct:**
      1. Plays "right". streak = 0 if it was negative, otherwise streak + 1.
      2. Speeds increase by +9.6 / +4 / +10.4. There is no maximum.
      3. Shows the next bucket apple and increments the colour count (max 3). If a flipper has been used, hints
         are turned off.
      4. Hides the apple and decrements apples-in-play.
      5. If both counts are 3: won = 1, plays "win" and PlayVoice(db0112). Otherwise, if this colour's count
         just became 1, plays PlayVoice(db0109).
    - **Wrong:**
      1. Plays a random voice (rand%3: 0 → db0110, 1 → db04150, 2 → db0111) unless a voice is playing
         (node+0x17C).
      2. Hides the apple, plays "boing" and shows the matching bounce anim at sequence 0.
      3. streak − 1. Decrements apples-in-play.
      4. If streak < −2 and all three speeds are above their minimum, all speeds drop by 9.6 / 4 / 10.4.
      5. Plays "wrong".
- **KeyDown** 0x403d70 (cheat):
  - Before the game starts, the first 3 typed chars are stored.
  - If the stored chars are "YmA", the key is 'B', the game is started, green == red == 2 and no egg exists:
    creates the anim "green apple Z" (+0x1F8) with sequence 1 and velocity (−100, 70, 0).
  - Otherwise passes the key to the toolbar KeyDown 0x41c310.
- **ListenToMessage** 0x402260 (this = node+0x18):
  - −0x66: passed to the toolbar only if not won.
  - 13000: sets app+0x33C = 1 and calls GotoNode("appleW").
  - 50000 (ambient timer):
    1. rand%4 == 0 plays "thunder", == 1 plays "thunder2", otherwise sets the leaf anim to sequence 0.
    2. Re-arms the timer at app+0x19C (vt+8) with (rand%5 + 1)·2000 ms.
  - Other messages: toolbar 0x41c910.
- **SoundEnded:** db0112 → plays the "whinney2" sound. whinney2 → PlayVoice(db04151). db04151 → sets
  app+0x33C = 1 and calls GotoNode("appleW").
- **Dtor:**
  1. Stops "GameAmb" and the ambient timer, deletes the rollovers.
  2. **If won: app+0x268++ and SaveGame(app+0x1B0, [0x809ac0]); sets the global u16 [0x80a160] = 1.**
  3. Closes GameBarb.omt, deletes the anims, then runs the HCToolbarNode dtor 0x41bc30.

Transitions: "appleW" on exit (message 13000 or after the win voices); the toolbar handles the rest.

| addr | name | purpose |
|---|---|---|
| 0x4015e0 | AppleSort ctor | builds the scene (see above) |
| 0x401bd0 / 0x401bb0 | dtor / scalar deleting dtor | saves the game if won |
| 0x401400..0x401500 | static init of the speed floats | 6 thunk + body pairs |
| 0x401520 / 0x401580 | ForceHideAnim / ForceShowAnim(anim) | cdecl: calls vt+0x30/vt+0x38 until hidden+stopped / shown+running |
| 0x4020e0 | ToggleFlipper(anim) | "lever", sets +0x266, toggles sequence 0↔1 |
| 0x402150 | SpendTime | drives Flippers/UpdateApple |
| 0x4021b0 | MouseDown | see above |
| 0x402260 | ListenToMessage | see above |
| 0x4023a0 | AddApple | see above |
| 0x402520 | UpdateApple | apple state machine |
| 0x4034f0 / 0x403710 / 0x403930 | SnapToLane / IsAtRow / SnapToRow | static helpers (anim, index) |
| 0x403a70 | ScriptCommand | initFlippers |
| 0x403ca0 | ScriptEnded | Flippers → AddApple |
| 0x403cd0 | SoundEnded | win chain → appleW |
| 0x403d70 | KeyDown | cheat |
| 0x403ed0/ee0/ef0/f10 | dtor adjustor thunks (−0xC/−0x18/−0x6C/−0x7C) | |

### Shared COMDAT helpers emitted in AppleSort.obj
- 0x401b80 / 0x401b90 / 0x401ba0: empty virtuals (`ret 4` / `ret 0x10` / `ret 0xC`). They fill HCNode hook slots
  +0x9C..+0xB8 in every node vtable.
- 0x403f00: adjustor thunk (this−0x6C → 0x45c880, main vtable slot +0x20). It is used by every node's +0x6C vtable.
- 0x402030 `String::String(const String&)` and 0x402090 `String::operator=(const String&)`. String = {char* p;
  u16 len} and 0x45eda0 = Reserve.
- 0x401df0 **SaveGame(record*, slot)** and 0x401ffe, its `catch(...)` funclet (frees the temporary String and
  resumes at 0x402010). See the save section below.

## Leapfrog — factory "leapfrog" (mission M2)

A frog-themed **triangular peg-solitaire** puzzle on 15 lily pads. The player must bring the **evil frog** to
the top pad (pad 0) to win a coin. Base: HCToolbarNode.
- ctor 0x41f1f0 `(app, name)`. Size **0x270**. Scalar deleting dtor 0x41f900. Dtor 0x41f9e0. Thunks
  0x420ed0/ee0/ef0/f00.
- Vtables: main 0x7f1d78. Overrides: +0x00 0x41f900, +0x54 MouseDown 0x41fcd0, +0x78 AnimEnded 0x420270,
  +0x80 SoundEnded 0x420e70, +0x98 ScriptCommand 0x420cf0. +0x0C 0x7f1d70 [0x420ed0, SpendTime 0x420b30];
  +0x18 0x7f1d60 [ListenToMessage 0x420d80, 0x420ee0]; +0x6C 0x7f1d58; +0x7C 0x7f1d48.
- Assets: opens `leapfrog.omt.cache` (prepend, cache), plus `leapfrog.omt` (Canv "leapfrog background"; HCAn/SMAK
  exit_*Frog, BarbieCutOut, bigCoin, Coin, CoinlessFrog, Ducks, EvilFrog, FrogAtRest, FrogSplash, Lilly1–15,
  NormalFrog, FrogTB*; SCR "Croak", "Ducks", "delayJump"; WAVE DB04119/120/121, babyduck…). Sounds: "frogloop"
  (ambient loop at volume 40, stored at +0x178), "coin", "Win", "Croak".."Croak4", "croak4". Voices
  db04119A–D, DB9065, DB9016, db9016n, db9015, DB04120, DB04121. Cursor "frogcursor" or "cursorOut" (at the
  screen edges). Toolbar art frogTBQuit/Pager/Camera/Map/Bag/Help/Home (0x41bf40).

| off | type | meaning |
|---|---|---|
| +0xC4, +0xCE | u16 | edge-exit enables (set to 1) |
| +0xD0 | int | node id = **19 (bridgeS)**, so the toolbar treats leapfrog as part of bridgeS |
| +0x1C0 | u16 | "click consumed" flag: the next mouse-up (−103) is swallowed |
| +0x1C4 | u32 | jump delay in ms for script "delayJump" (script var "delay", via "initDelay") |
| +0x1C8 | u32 | index of the pad that was jumped over |
| +0x1CC | u32 | unused flag (always 0; gates NudgeEvilFrog) |
| +0x1D0 | u32 | won (the evil frog reached pad 0) |
| +0x1D8 | s32 | selected pad (−1 = none) |
| +0x1DC..+0x214 | LeapfrogPad*[15] | pads ("lilly1".."lilly15", hotspot ids 1234+i) |
| +0x218 | anim* | evil frog |
| +0x234/+0x238/+0x23C | ACRollover* | for coin / coinlessFrog / bigCoin (0x401160) |
| +0x240 | anim* | frog currently moving |
| +0x244 / +0x248 / +0x24C / +0x254 / +0x258 | anim* | ducks / coin (id 0x22E5) / BarbieCutOut (id 0x22E6) / coinlessFrog (id 0x22E7) / bigCoin (id 0x22E8) |
| +0x250 | anim* | frog being jumped over |
| +0x25C | u32 | (ctor sets 0) |
| +0x260 / +0x264 | u32 | jump in progress / step in progress |
| +0x268 / +0x26C | s32 | move source pad / target pad |

Board: pad i holds a frog anim in `pad+0xC8`. Two lookup tables of 15×6 s16: **0x800ef0** jump targets per
direction and **0x800fa8** the adjacent pad in the same direction (−1 = none). Pads form a 5-row triangle
(0 / 1 2 / 3 4 5 / 6 7 8 9 / 10..14). Setup: srand(time). e = rand()%15 is the empty pad. v = 6 + rand()%9 (≠e)
is where the evil frog starts. Every other pad gets a NormalFrog anim (instance name "frog<n>").
Rules (MouseDown on a pad, ids 1234..1248):
- Nothing selected: an empty pad → voice DB04120. A pad with a frog → select it and play a random Croak..Croak4.
- Clicking the selected pad → deselect + DB04120. Clicking another pad that holds a frog → select that pad
  instead.
- Target empty, selected = **normal frog**: there must be a valid jump over an occupied pad (0x4201c0), else
  the "croak4" sound. The jumped pad must not hold the evil frog → StartJump. Normal frogs never step.
- Target empty, selected = **evil frog**: a valid jump → StartJump + voice DB04121. Otherwise a step to an
  adjacent empty pad (0x420220) → StartStep. Otherwise DB04120.
- Input is ignored while a frog is moving or a move is in progress.
- Animation: jump sequence = {3,5,1,4,6,2}[direction], delay {1400, 800, 800, 1400, 1100, 1100} ms. Step
  sequence = {3,5,1,2,6,4}[direction]+6, delay {1300, 700, 700, 1300, 1000, 1000} ms (indexed by the sequence
  value 1..6). The z-order counter 0x80105c (starting at 3001.0) increments on each move.
- When the jump sequence ends (AnimEnded, seq 1..12): FinishJump moves the frog to the target pad and gives the
  jumped frog sequence 7/8/9/10. Which one depends on the jumped pad: {1,3,6}→7, {2,4,7,11}→8, {5,8,12}→9,
  {9,13}→10. Then the "Croak" script runs. FinishStep is the same without a victim.
- **Win**: the evil frog lands on pad 0 → evil frog sequence 9, sound "Win", +0x1D0=1. When sequence 9 ends,
  the evil frog is hidden and replaced by CoinlessFrog, the coin is shown, and the "coin" sound and voice db9016n
  play. Clicking CoinlessFrog (0x22E7) does the same.

| addr | name | purpose |
|---|---|---|
| 0x41f1f0 | ctor | see above. If app+0x25C (leapfrog coins) == 0, queues the instruction voices pause, pause, db04119A–D, DB9065. Registers with the mouse broadcaster |
| 0x41f9e0 | dtor | Stops the ambient sound, deletes the rollovers and pads, deletes ~36 named anims, ~HCToolbarNode |
| 0x41f920 | NormalizeAnimOrigin(anim) | Shifts every frame of every sequence so that each sequence's first frame sits at the origin, then sets the position to (0,0,z) |
| 0x41fc80 | PlaceFrogOnPad(frog, pad) | Frog position = the pad anim's current frame position + (0,5). pad+0xC8 = frog |
| 0x41fcd0 | MouseDown | Game rules above. 0x32CE (toolbar Help) → replays db04119A–D. 0x22E5 coin → see Transitions. 0x22E6 BarbieCutOut → GotoNode("leapfrog") (restart). 0x22E8 bigCoin (after a win) → hide it, voice db9015 |
| 0x4201c0 | FindJumpDirection(a,b) | direction k with jump[a][k]==b and the middle pad occupied, else −1 |
| 0x420220 | FindStepDirection(a,b) | direction k with adj[a][k]==b and b empty, else −1 |
| 0x4207d0 | GetJumpedPad(a,b) | adj[a][k] for the jump direction |
| 0x420820 / 0x420890 | GetJumpSequence / GetStepSequence | maps a direction to an anim sequence |
| 0x4203e0 / 0x420900 | StartJump / StartStep | Lift the frog, choose the sequence and delay, run script "delayJump" |
| 0x420580 / 0x420a10 | FinishJump / FinishStep | Land the frog; the victim's anim; Win check |
| 0x420ad0 | Win | |
| 0x420270 | AnimEnded | Win reveal, landing dispatch, NudgeEvilFrog |
| 0x420cd0 | NudgeEvilFrog | Moves the evil frog by (34, 58). Dead path (+0x1CC is never set) |
| 0x420b30 | SpendTime | Every 30 s replays the ducks anim and the "ducks" script. Cursor "cursorOut" when mouse x < 15 or > 604 (sets +0xC4=1), else "frogcursor". Then the toolbar SpendTime |
| 0x420cf0 | ScriptCommand | "duck", "babyduck", "frog2" → ignored. "initDelay" → SetScriptVar(script, "delay", +0x1C4). Otherwise the toolbar handler |
| 0x420d80 | ListenToMessage | −103 while +0x1C0 is set → clear the flag and swallow the event. 13000 (edge click from the toolbar) with mouse x < 15 or > 604 → app+0x33C=1, GotoNode("bridgeS"). Otherwise the toolbar handler |
| 0x420e70 | SoundEnded | When "db9015" ends after a win → app+0x33C=1, GotoNode("bridgeS") |

Transitions and saves: clicking the coin (0x22E5): hide the coin, create bigCoin (z+100) with an ACRollover,
voice DB9016, **app+0x33C=1, app+0x25C += 1, SaveGame(app+0x1B0, slot)**, global 0x80a168=1,
AddBagItem(0x18, 1), app+0x314=7. Exits: "bridgeS" (screen edge, or the end of the win voice); "leapfrog" (Barbie
cut-out restarts the game).

### LeapfrogPad *(name inferred)*

A trivial subclass of the 0x40ec40 anim-hotspot helper (listener 0x45bbd0 base + Rollover at +0x10; anim at
+0xAC). It adds **+0xC8 frog* occupant**. Size **0xCC** (`new(0xCC)`).
- No ctor function (inlined: 0x40ec40(node, 0), then vtables 0x7f1d38 / +0x10 0x7f1d28, +0xC8=0).
- Dtor 0x420f30 (resets the vtables and jumps to the base dtor 0x40ed00). Scalar deleting dtor 0x420f10.
  Thunk 0x420f50 (this−0x10).
- Vtable 0x7f1d38: [0x40f390, 0x420f10, 0x45bd80, 0x40ed80]. 0x7f1d28: [0x420f50, 0x426b90, 0x40f330, 0x40f360].
  Only the dtor is overridden.

## BellGame — factory "bellgame" (mission M7)

The "ring the bell" mini-game at the bridge. The player picks up objects (the cursor becomes the object) and uses them on the
bell pole ("poll") in the right order. The pole then becomes ringable. Ringing it plays a movie and counts the completion.
Base: HCToolbarNode (ctor 0x41b690). Size 0x25C. ctor 0x40a980, dtor 0x40ab90, sdd 0x40ab70.
- Vtables: main 0x7f0b10 (55 slots): +0x00 sdd, +0x54 MouseDown 0x40aeb0, +0x6C KeyDown 0x4140e0 (shared forwarder to toolbar
  0x41c310), +0x78 AnimEnded 0x40b4e0, +0x98 ScriptCommand 0x40b790. +0xC 0x7f0b08 [thunk 0x40bb50, SpendTime 0x40bab0];
  +0x18 0x7f0af8 [ListenToMessage 0x40ad70, thunk 0x40bb60, …]; +0x6C 0x7f0af0 [thunk 0x40bb70, …]; +0x7C 0x7f0ae0 [thunk 0x40bb80, …].
- Assets: `bellgame.omt.cache` (OpenResourceFile prepend=1,cache=1) in ctor; `GameBarb.omt` opened in SetupScene and closed in dtor;
  script `GameAmb` (RunScript; stopped in dtor). Anims: `poll` (bell pole, id 0x577E), `waterfall`, `Bellgameoverlay1..6`,
  `bellgameObject1..3` (instance names `cursorBellGame1..3`, ids 0x577F..0x5781), `GameBarbie`, `GameFlaps`. The dtor also deletes
  `GrassOverlay`, `BellgameObject1..8`, `BellgameOverlay1..5`, `BellGameOverlay6`, `Poll`, `RingBell`, `WaterFall`. Objects 4..8
  (ids 0x5782..0x5786) are handled in MouseDown, but this class never creates them; the script probably does *(inferred)*.
  Sounds: `waterfall` (ambient; stored in toolbar node+0x178), `ring1`/`ring2`, `hinge`, `Rope`, `Hinge`, `teapot`, `hammer`, `candle`,
  `Candle`, `complete`. Voices: `pause`,`pause`,`DB04146N` (intro), `DB0157`, `DB0458`, `DB9020N`, `DB9019` (wrong object), `DB0156`,
  `Db03027N` (Barbie clicked), `DB9023` (reminder every 30 s after completion).
  SMK: first completion → `smk\MH7.smk`; later completion n → `smk\BellMH1.smk` (n%3==0), `BellMH2` (1), `BellMH3` (2).
  The path is relative: app+0xE4 is copied into the buffer first and then overwritten.

| off | type | meaning |
|---|---|---|
| +0x1C0/+0x1C2 | u16 | set by msg −0x66 when the mouse is at x<350 (y≥200 / y<200). It only fires if +0x1D4==1, which nothing in this class sets: looks dead *(inferred)* |
| +0x1C4 | u16 | bell rung / game complete (blocks re-completion; enables the 30 s DB9023 reminder) |
| +0x1C8 | s32 | puzzle state 0..4 (4 = pole ready to ring) |
| +0x1CC | HCMovie* | completion movie (0x453240, 0x124 bytes); deleted in SpendTime when movie+0xF8 is set |
| +0x1D0 | s32 | carried object index (−1 = none) |
| +0x1D4 | s32 | 0 (see +0x1C0) |
| +0x1D8 | HC2DAnim* | bell pole anim `poll` |
| +0x1DC..+0x1E4 | HC2DAnim*[3] | bellgameObject1..3 |
| +0x21C..+0x230 | HC2DAnim*[6] | Bellgameoverlay1..6 (anim+0x62 = 1; overlay4 moved by (−1, 7, 0)) |
| +0x234 | HC2DAnim* | waterfall |
| +0x238 | helper* | 0xA0-byte cursor-hotspot helper (ctor 0x401160(node, anim, 7)) for the pole |
| +0x23C..+0x244 | helper*[3] | same helpers for objects 1..3 |

Puzzle (MouseDown, ids): clicking an object (0x577F..0x5786) while the state is not 4 picks it up (0x40ba00) and plays its
sound (0x577F hinge, 0x5781 Rope, 0x5782 Hinge, 0x5784 teapot, 0x5785 hammer, 0x5786 candle, 0x5780/0x5783 silent).
Clicking the pole (0x577E) applies the carried object index:
idx0 with state 0 → pole seq 1, state 1, voice DB0157 · idx1 with state 1 → seq 3, state 2, DB0458 · idx1 with state 3 → seq 4, state 4, DB9020N ·
idx2 with state 2 → seq 4, state 4, DB9020N + Rope · idx2 with state 1 → seq 2, state 3, DB0157 + Rope · any other idx0..2 use → DB9019 ·
idx3..7 → DB0156 (idx7 also plays Candle). Every use puts the object back (un-hidden) and restores `cursorOut`.
Clicking the pole while the state is 4 → seq 5 plus ring1/ring2 at random. AnimEnded(pole, seq 5) with +0x1C4==0 → OnBellRung.

| addr | name | purpose |
|---|---|---|
| 0x40a980 | ctor | toolbar ctor; global 0x809ad4=1; open bellgame.omt.cache; node+0x108=1; toolbar buttons tb*Out 0..6; 0x41c1f0(viewport x,y); cursor `cursorOut`; node+0xC4=1; cursor anim z=5000; ambient `waterfall` (PlaySound ch −1, vol 40, keep); SetupScene; init state; voices pause,pause,DB04146N (no flap); register node+0x18 with global broadcaster 0x80a5a0 |
| 0x40ab70 | sdd | |
| 0x40ab90 | dtor | delete helpers, stop GameAmb, close GameBarb.omt, delete anims, delete movie, **app+0x33C=1**, toolbar dtor 0x41bc30 |
| 0x40ad70 | ListenToMessage | msg 13000 (screen-edge click): app+0x33C=1, GotoNode("bridgeS"). msg −0x66: see +0x1C0. Others → toolbar 0x41c910 |
| 0x40aeb0 | MouseDown | puzzle above; 0x32CE (Barbie hotspot *(inferred)*) → voice Db03027N; always ends in toolbar MouseDown 0x41c340 |
| 0x40b4e0 | AnimEnded(anim,seq) | pole seq 5 finished → OnBellRung; then toolbar 0x41c250 |
| 0x40b520 | SetupScene | create the anims listed above, hotspot helpers, open GameBarb.omt, RunScript GameAmb, GameBarbie at (342,−25,150) with flag 8, GameFlaps at (342,−25,0) |
| 0x40b790 | ScriptCommand | forwards to toolbar 0x41dcd0 |
| 0x40b7b0 | OnBellRung | once: +0x1C4=1, sound `complete`, **app+0x270 += 1, SaveGame**, 0x41e8f0 (mission-complete voice; it increments app+0x2FC), open and play the SMK at z=2000; if the count is >1 and count%3 ∈ {0,1}, move GameBarbie to (192,−25,0). Logs "Smacker File Failed to Load!" on failure |
| 0x40ba00 | PickUpObject(id) | hide the clicked object; un-hide the previously carried one; carried=id−0x577F; SetCursor("bellgameObject%d", id−0x577E); cursor anim z=5000, flag 8 |
| 0x40bab0 | SpendTime | delete the finished movie (re-enable node+0xC4); after completion, play voice DB9023 every 30 s (static tick 0x7feda0); toolbar SpendTime |
| 0x40bb50/60/70/80 | thunks −0xC/−0x18/−0x6C/−0x7C | |
| 0x40ae90 | (Rollover::MouseEnter) | empty `ret`, linker-shared; used by vtbl 0x7f33a0 +8/+0xC |
| 0x40aea0 | atexit dtor | empty destructor for ListenToMessage's local static float pair (0x809ac8/0x809acc, guard 0x809ad0) |

Transitions: msg 13000 → "bridgeS". Save: 0x40b7b0.

## VineGame — factory "vinegame" (mission M8)

This mini-game is reached from the rainbowS trail (mission M8). The player pulls 7 rows of flowering vines
off a bush to uncover a horseshoe. Base class: HCToolbarNode (0x41b690). Size **0x28C** (`new` in
0x412140).
- Constructor 0x43c0c0, destructor 0x43c770, scalar-deleting destructor 0x43c750.
- Main vtable **0x7f3e98**. It overrides +0x00 (0x43c750), +0x54 MouseDown (0x43c930) and +0x80 SoundEnded (0x43cd40).
  All other slots are the HCToolbarNode/HCNode ones (Start is 0x454c10).
- Secondary vtables:
  - +0x0C at 0x7f3e90: [0x43cd60 dtor adjustor, 0x41cba0 SpendTime from the base].
  - +0x18 at 0x7f3e80: [0x43cd00 ListenToMessage, 0x43cd70 dtor adjustor, 0x45bd80].
  - +0x6C at 0x7f3e78: [0x43cd80, 0x403f00].
  - +0x7C at 0x7f3e68: [0x43cd90, 0x45c580, 0x45c510, 0x45c470].
- Assets:
  - Anims `<c>flower1`, `<c>flower2` and `<c>flower3` for each c in red, orange, yellow, green, blue, indigo, violet (name table at 0x803ea8).
    For rows 2, 3, 4 and 6, once the horseshoe has already been found (app+0x274≠0), the middle anim is
    `<c>2flower` instead (table at 0x803ec8).
  - `vines` anim, or `vines2` when app+0x274≠0.
  - Cursor `vinecursor`.
  - Toolbar button names quit/pager/camera/map/saddlebag/help/home.
  - Ambient loop `falls` (volume 0x28).
  - Script `GameAmb`.
  - Sounds `tugvine`, `horseshoe`, `null`.
  - Voices db9021 (help, also played 1500 ms after start), db03030, db03045, db0178, db03046, db03047, db03052,
    db04069, db04071, db04070.

| off | type | meaning |
|---|---|---|
| +0x0C4 | u16 | =1: clicking either screen edge sends msg 13000 (handled by the HCToolbarNode ListenToMessage at 0x41c910) |
| +0x0D0 | int | node id = 0x15 (rainbowS), so the toolbar and map treat the game as part of rainbowS |
| +0x178 | char* | ambient loop sound (`falls`) |
| +0x1C0/+0x1C4/+0x1C8 | anim* | scratch pointers used while building each row |
| +0x1CC | HC2DAnim* | `vines` (click id 1000). Its sequence advances once per row pulled and twice more for the horseshoe |
| +0x1D0[21] | HC2DAnim* | flower anims, 3 per row (click id 0x3EA = inert, 0x3E9 = pullable) |
| +0x224[21] | ACRollover* | one hotspot per flower anim (cursor 7) |
| +0x278 | int | state: 0 pulling, 1 all 7 rows pulled, 2 horseshoe shown, 3 horseshoe taken |
| +0x27C | u16 | first-pull voice pending (=1 at construction) |
| +0x27E | u16 | =1 at construction (meaning unknown) |
| +0x280 | int | rows activated so far (1..7) |
| +0x284 / +0x288 | int | index of the next anim to hide / to activate |

| addr | name | purpose |
|---|---|---|
| 0x43c0c0 | VineGame::VineGame | Builds 7×3 flower anims with hotspots, activates row 0, sets up the toolbar, vines anim and cursor, registers with the broadcaster at 0x80a5a0, starts the `falls` loop and `GameAmb`, and queues db9021 after 1500 ms. |
| 0x43c750 | VineGame::\`scalar deleting destructor' | |
| 0x43c770 | VineGame::~VineGame | Stops the ambient loop and `GameAmb`, deletes the vines anim and the 21 anims and hotspots, then runs ~HCToolbarNode (0x41bc30). |
| 0x43c850 | VineGame::AdvanceVineRow | Hides the 3 anims of the current row (vt+0x30(1)) and gives the next 3 click id 0x3E9. After row 7 it sets state=1. |
| 0x43c930 | VineGame::MouseDown | Click state machine (below). Ends by calling the toolbar MouseDown 0x41c340. |
| 0x43cd00 | VineGame::ListenToMessage | this=node+0x18. Msg 13000 (screen-edge exit): sets app+0x33C=1 and goes to `rainbowS`. Any other message goes to 0x41c910. |
| 0x43cd40 | VineGame::SoundEnded | Forwards to HCToolbarNode::SoundEnded 0x41c460. The same body is also used by Stable's vtable 0x7f3530 (identical-code folding). |
| 0x43cd60/70/80/90 | VineGame::\`scalar deleting destructor'\`adjustor{12/24/108/124}' | `sub ecx,N; jmp 0x43c750` |

MouseDown, by click id (the clicked element's +0x11C):
- **0x3E9** (pullable flower):
  1. Advances the vines sequence, calls AdvanceVineRow and plays `tugvine`.
  2. If state is now 1, it says db03045 (horseshoe already found, app+0x274≠0) or db03030, after a 500 ms delay.
  3. Otherwise, on the first pull it says db0178. After that it picks a random line: db03052, db03046 or db03047 (PlayVoicePaused).
     A random roll of 3 or 4 says nothing.
- **1000** (vines):
  - In state 1 with app+0x274==0: advances the sequence, plays `horseshoe`, says db04069 (app+0x29E==0) or db04071,
    then db04070 (PlayVoicePaused), and **saves the game**. State becomes 2.
  - In state 1 with app+0x274≠0: state becomes 2 and nothing else happens.
  - In state 2 with app+0x274==0: advances the sequence, sets state=3, increments **app+0x274**, and calls
    **AddBagItem(0 horseshoe)**. There is no save here.
  - If all eight mission counters app+0x258..0x274 are then nonzero, it sets app+0x33C=1 and goes to `rainbowS`.
- **0x3EA**: plays sound `null`.
- **13000**: sets app+0x33C=1 and goes to `rainbowS`. **0x32CE** (help): db9021.

Transitions: either screen edge, or finishing M8 with all missions done → `rainbowS` (app+0x33C=1 means "resume at the stop").
Save: once, when the horseshoe is revealed in state 1, which happens *before* app+0x274 is incremented.

## Carousel — factory "carousel" (cottage interior, mission M4)

Inside of the trail cottage: plays the door movie, then Barbie walks around (script `CarouselWalk`); clickable
quilt (6 picture squares with voice lines), chest (opens once; flag saved) and the carousel music box (plays
`smk\Carousel.smk` with the `MusicBox` sound). Edge click returns to the `cottageL` trail node.

- Base: HCToolbarNode (0x41b690). Factory `new(0x224)`.
- ctor 0x40f860, dtor 0x40fa70, scalar deleting dtor 0x40fa50.
- Vtables: main 0x7f0f28 (overrides +0x00 dtor, +0x54 MouseDown 0x410050, +0x6C KeyDown 0x410020,
  +0x78 AnimEnded 0x410150, +0x98 ScriptCommand 0x410420; rest inherited); +0xC 0x7f0f20 [0x410ce0 thunk dtor,
  SpendTime 0x40fb90]; +0x18 0x7f0f10 [ListenToMessage 0x40fd10, thunk dtor 0x410cf0, 0x45bd80];
  +0x6C 0x7f0f08 [0x410d00 thunk dtor, 0x403f00]; +0x7C 0x7f0ef8 [0x410d10 thunk dtor, 0x45c580/0x45c510/0x45c470].
- Assets: `carousel.omt.cache` (opened in ctor), `trlAnims.omt`, `trails.omt` (opened in BuildScene, closed in dtor);
  movies `smk\OpenDoor.smk` (entry), `smk\Carousel.smk` (music box); anims barbieWalkOutside, barbie_ambients, bed,
  door, quilt, quiltCloseup, chest, chestCloseup, carousel; toolbar buttons tbQuitOut…tbHomeOut, cursor `cursorOut`;
  script `CarouselWalk` (vars initWalk→cycles/direction/lastDirection); sounds `MusicBox` (channel 13, 9999 loops),
  `square`; voices DB0080, DB0081N, DB0090, DB0096, DB9045, DB9024, DB04129–DB04137, DB9046, DB9047, DB0954.

| off | type | meaning |
|---|---|---|
| +0xC4 | u16 | (toolbar) edge-exit enabled: 1 in ctor, 0 while door movie plays, 1 after movie |
| +0xEC | ptr | (toolbar) cursor anim, placed at z=5000 in ctor |
| +0xFC / +0x108 / +0x160 / +0x164 | | toolbar fields set to 1 / 1 / 0x31d / 0 in ctor *(meaning unknown)* |
| +0x1C0 | u16 | swallow next click (set after chest close-up click) |
| +0x1C2 | u16 | quilt was just clicked (swallows the click that opened the close-up) |
| +0x1C4 | u16 | scene not yet built (1 in ctor; BuildScene+StartWalk run when the door movie ends) |
| +0x1C8 / +0x1CC | anim | barbieWalkOutside / barbie_ambients |
| +0x1D0 / +0x1D4 | anim | bed (offset 200,0,0) / door (z=300) |
| +0x1D8 / +0x1DC | anim | quilt (hotspot 0x24B6) / quiltCloseup (hidden) |
| +0x1E0 / +0x1E4 | anim | chest (0x24B7, sequence = chest-opened) / chestCloseup (hidden, 0x24B9 when shown) |
| +0x1E8 | anim | carousel (0x24B8, offset 200,0,0) |
| +0x1EC / +0x1F0 / +0x1F4 | ACRollover* | rollovers for quilt / chest / carousel |
| +0x1F8 | HCMovie* | current movie (OpenDoor or Carousel) |
| +0x1FC | u32 | quilt close-up visible |
| +0x200 | u32 | movie state: 2 = door movie, 1 = door movie click-acknowledged *(inferred)*, 0 none |
| +0x204 | u32 | chest close-up visible |
| +0x208 | u32 | quilt viewed during this visit |
| +0x210 | u32 | first walk pending (1 in ctor → walker placed at (-335,0,100)) |
| +0x214 / +0x218 / +0x21C | s32 | lastDirection / cycles / direction for the walk script |
| +0x220 | s32 | last click x |

Methods:

| addr | name | purpose |
|---|---|---|
| 0x40f860 | Carousel::Carousel | open carousel.omt.cache, toolbar "Out" buttons, cursorOut, init fields, [0x809afc]=1, start OpenDoor movie, app+0x264 += 1, SaveGame, register listener with broadcaster [0x80a5a0]+0xC |
| 0x40fa50 | `scalar deleting destructor'` | |
| 0x40fa70 | ~Carousel | delete rollovers, close trlAnims/trails.omt, delete scene anims, toolbar dtor |
| 0x40fb90 | SpendTime (+0xC) | no movie: when walk script ends swap walker for ambient anim (x ± offset, random sequence 0–2); movie finished: build scene if needed, delete movie, enable edge exits |
| 0x40fd10 | ListenToMessage (+0x18) | 13000: close quilt close-up (quilt viewed → app+0x2a2=1) / leave to cottageL (app+0x33c=1) / abort music-box movie; left-up: dismiss chest close-up, skip door movie, quilt squares, or start walk toward click (cycles=|(x-166)/200|) |
| 0x410020 | KeyDown | swallows key code 1 while a movie is playing, else toolbar KeyDown |
| 0x410050 | MouseDown | hotspots 0x24B6 quilt, 0x24B7 chest, 0x24B8 music box, 0x24B9 chest close-up (first time voice DB04137, app+0x2a6=1, chest anim sequence 1) |
| 0x410150 | AnimEnded | walker anim ended → toolbar SetScrollVelocity(10.0) |
| 0x410180 | BuildScene | open trlAnims/trails.omt, create all scene anims and rollovers, voice DB0080 |
| 0x410420 | ScriptCommand | "initWalk": push cycles/direction/lastDirection into the script |
| 0x4104a0 | StartWalk | show walker, hide ambient, run `CarouselWalk` unless a close-up is open |
| 0x410540 | PlayMusicBox | sound MusicBox ch13, voice DB9045, play smk\Carousel.smk at viewport x, z=2000 |
| 0x410660 | OnQuiltClick | voice DB0081N (first) / DB0090, show quilt close-up |
| 0x4106c0 | OnQuiltSquareClick | 3×2 grid x 122/254/387/522, y 78/208/345 → close-up sequence 1–6, sound `square`, voices (square 5 depends on app+0x274); click outside with y<400 closes (app+0x2a2=1) |
| 0x410b50 | OnChestClick | voice DB04136 / DB9024, show chest close-up with sequence = opened |
| 0x410be0 | PlayOpenDoorMovie | play smk\OpenDoor.smk (z=4000), state 2, disable edge exits |
| 0x410ce0/0x410cf0/0x410d00/0x410d10 | thunk dtors | this-adjust −0xC/−0x18/−0x6C/−0x7C → 0x40fa50 |

Transitions: edge click (13000) with no close-up and no movie → `GotoNode("cottageL")` after app+0x33c=1.
Save: ctor increments app+0x264 and calls SaveGame(app+0x1b0, [0x809ac0]).

## OpenDoor — factory "opendoor"

Plays the full-screen `smk\OpenDoor.smk` (stable door opening) with the "dooropen" sound, then goes to the carousel.
Base: HCToolbarNode.
- ctor 0x421200 `(app, name)`. Size **0x1C4**. Scalar deleting dtor 0x421420. Dtor 0x421440. Thunks
  0x421530/540/550/560.
- Vtables: main 0x7f3000 (only +0x00 overridden); +0x0C 0x7f2ff8 [0x421530, SpendTime 0x4214e0];
  +0x18 0x7f2fe8 [ListenToMessage 0x421510, 0x421540]; +0x6C 0x7f2fe0; +0x7C 0x7f2fd0.
- Assets: `opendoor.omt` (Canv/BLIT "opendoor background", WAVE "dooropen"); `<app+0xE4 CD path>smk\OpenDoor.smk`.
- Fields: +0xC4 u16 = 0 (edge exits off); +0x1C0 HCSmackerElement* (12 fps, palette on).

| addr | name | purpose |
|---|---|---|
| 0x421200 | ctor | **SaveGame(app+0x1B0, slot) immediately.** Registers with the mouse broadcaster. Builds the path. new HCSmackerElement(12,1)->Open(path, preload=1); on failure logs "Smacker File Failed to Load!". Otherwise: attach, loop=0, show, SetPosition(viewport+0x40, 0, z 5000), GotoFrame(1), unpause, play "dooropen" (volume 0x70) |
| 0x421440 | dtor | pause and hide the movie, delete it, ~HCToolbarNode |
| 0x4214e0 | SpendTime | movie done (+0xEE) → GotoNode("carousel"). Does not call the toolbar SpendTime |
| 0x421510 | ListenToMessage | −103 (mouse up) → GotoNode("carousel") (skip). Does not forward other messages |

## Race — factory "race"

A side-scrolling horse race: Barbie against a friend (Christie or Teresa) over 20 screens (12800 px). Includes
jumps over a stream or rocks, competitor rubber-band AI, and win/lose bookkeeping. Base: HCToolbarNode.
- ctor 0x422d00 `(app, name)`. Size **0x284**. Scalar deleting dtor 0x423ca0. Dtor 0x423cc0. Thunks
  0x4269a0/9b0/9c0/9d0.
- Vtables: main 0x7f32c0. Overrides: +0x00 0x423ca0, +0x54 MouseDown 0x4242f0, +0x6C KeyDown 0x424000,
  +0x70 KeyUp 0x424080, +0x7C AnimFrameCue 0x4266b0, +0x90 ScriptEnded 0x4263e0, +0x98 ScriptCommand 0x426140.
  +0x0C 0x7f32b8 [0x4269a0, SpendTime 0x424390]; +0x18 0x7f32a8 [ListenToMessage 0x424100, 0x4269b0];
  +0x6C 0x7f32a0; +0x7C 0x7f3290.
- Static data: 0x422cc0/0x422cd0 = CRT initializer: gCompetitorVolume 0x80a110 = 0x70−0x20 = 80.
  0x422ce0 = SetCursorPos(x,y) wrapper (also used by the trail ride).
- Course: **+0x1FC** = 1 if app+0x308 == 1, 0 if app+0x308 == 2 (any other value logs "raceChallenge not set" and
  uses 1). Course 0 = beach: ambient "xtrr001", sand* sounds, gulls, start tile "arch", generic tiles "generic0
  race"/"generic1 race" alternating, finish tile "turtle rock", rival **RaceTeresa**, obstacle anims
  "stream"/"rocks", exit "beachE". Course 1 = "rrr" (rabbit run): "xrrt001", grass* sounds, "rrr mailbox",
  "rrr 2"/"rrr 0"/"rrr 1" cycling, "rrr old oak", rival **raceChristie**, "rrr logs"/"rrr rocks", exit "rabbitE".
  The background is a 20×1 tile layer of 640×480 tiles (0x477ad0 → node canvas, 0x45cd30).
- Horse: app+0x1C0 (0 palomino, 1 white, 2 paint, 3 dapple; 4 is replaced by rand()%4 **and written back**).
  Opens the matching `racePal/raceWht/racePnt/raceDpl.omt` (closed in the dtor) for the anim "race palomino" / "race
  white" / "racePnt" / "race dapple". The start sound is racepalo/racewhit/racepaint/racedap. Also the anims
  raceBarbie (rider), startBarbie, start flag (id 10000, ACRollover), end flag (x 12160). Fancy outfit
  (app+0x1C4 == 1; a value of 2 is reset to 0 and written back): raceFancy + hRaceFancy.
- Scripts (race.omt): raceStart, raceCStart, raceRun, raceCRun, raceJump, raceCJump, raceAbort, raceAbortGo.
  Script vars: horse, fancy, startSound, startSeq, competitor, jumpseq (stream → 4, rocks → 5; table 0x7f3220).

| off | type | meaning |
|---|---|---|
| +0xC8 | u16 | right-edge exit enabled (the toolbar sends 0x32CC on a click at x > 604) |
| +0x1C0 / +0x1C4 | u32 | accelerate / brake input **released** (1 = not held). Mouse: left/right button up = 1, down = 0. Keyboard (app+0x1A8==1): key 8 / key 9 up = 1, down = 0 |
| +0x1C8 | u16 | jump request (double-click < 400 ms, or key 3) |
| +0x1CA | u16 | intro running (inputs ignored until "raceStart" ends) |
| +0x1CC/+0x1D0/+0x1D4/+0x1D8/+0x1DC/+0x1E0 | anim* | Barbie horse / rider / raceFancy / hRaceFancy / startBarbie / competitor |
| +0x1E4 | anim* | winner (the first horse whose x > 11810) |
| +0x1E8 / +0x1EC | anim* | stream / rocks obstacle |
| +0x1F0 / +0x1F8 | anim* / ACRollover* | start flag / its rollover |
| +0x1FC / +0x200 | u32 | course / rival index (0 Christie, 1 Teresa) |
| +0x204 | u32 | current obstacle type (0 stream, 1 rocks) |
| +0x208..+0x21C | float[6] | obstacle x positions (multiples of 640) |
| +0x220 / +0x224 | u32 | obstacle count / current obstacle index |
| +0x228 / +0x22C | char* | current Barbie / competitor gallop sound |
| +0x236 / +0x238 | u16 | race started (start flag clicked) / race finished (both horses stopped) |
| +0x23A / +0x23C / +0x23E | u16 | running / start scripts launched / result comment played |
| +0x240 | u16 | fancy outfit |
| +0x244 / +0x248 | u32 | now / last tick |
| +0x24C | s16 | **Barbie speed** (scroll px/s *(inferred)*) |
| +0x254 | u32 | gait step 8/10/12/15 |
| +0x258 / +0x25A | u16 | competitor jumping / Barbie refusing (abort) |
| +0x25C | u16 | gull sounds played (max 3, course 0) |
| +0x260 / +0x268 | u32 | competitor speed level (from app+0x238) / obstacle level (from app+0x240) |
| +0x274/+0x278/+0x27C | u32 | competitor min / mid / max speed: level 0 = 100/170/250, 1 = 150/220/300, 2 = 200/270/350 |
| +0x280 | u16 | competitor sounds enabled (ctor sets 1) |

Setup rules (ctor):
- **Level promotion**: scan the record array app+0x1E8[5] (stride 0x14): n1 = entries with .+0 == speed level,
  n2 = entries with u16 .+4 ≠ 0, n3 = entries with .+0x10 == 0. If n1 ≥ 3 && n2 > 3, the speed level goes 0→1 or
  1→2. If n1 ≥ 3 && n3 > 3, the obstacle level goes 0→1 or 1→2. Promotions are local only; app+0x238 and
  app+0x240 are not written.
- **Obstacles**: level 0 → 1 obstacle at (6+rand%6)·640 or 2 at (3+rand%4)·640 and (11+rand%4)·640. Level 1 →
  3 or 4 obstacles, spaced (3..5)·640 apart starting at (2..4)·640; the 4th only if the 3rd is ≤ 7680. Level 2 →
  5 or 6 obstacles spaced (1..3)·640 apart. The first obstacle is stream or rocks at random (z 50). The next
  ones alternate (UpdateObstacles).
- Then app+0x188->vt+0x14(course 1 ? 10 : 1) (pager page *(inferred)*), and RunDelay(3000). The "delay" script
  ending while the start flag is still up → voice db04089.

Race loop (SpendTime 0x424390, this = node+0xC):
- Keeps the rider and fancy anims on the horse.
- Countdown after the start flag is clicked: a random voice db0486/7/8, then after 0/700/1200 ms the raceStart and
  raceCStart scripts. After 3300 ms (4300 ms for the white horse), raceMusic starts if music is on (app+0x344==0),
  and the cursor is centred.
- Speed: left held → +10 every ≥200 ms up to 300. At 300 the tired timer (CheckHorseTired 0x425da0) starts:
  10 s at 300 → "tired" for 5 s and speed forced to 260. Right held → −10 every 200 ms (no change below 151). No
  input → every 2 s, +10 if < 151 else −10. Mode app+0x1A8==2 (not settable in prefs): mouse y offset > 50 px
  from 240 drives the speed, and the cursor is re-centred.
- Gait sound and anim rate: speed < 200 → *08 (rate 125), < 250 → *10 (100), < 300 → *12 (83), else *15 (66).
- Jumps (UpdateObstacles 0x425240): a pending jump request waits while the horse x is in
  (obstacle−700, obstacle−200). Anywhere else, TryStartJump runs at once (raceJump script, log "Jump started
  at %6.0f", speed set to 220, gait 10). No request with the horse in (obstacle−100, obstacle) → refusal: raceAbort
  script, gait 12, and the speed is **set to** −30 on course 1 or −15 on course 0 (the horse backs up). Later
  raceAbortGo sets the speed to 300. The competitor jumps automatically while in (obstacle−200, obstacle).
  Passing an obstacle (viewport x > obstacle+320) moves the stream/rocks anim to the next obstacle position,
  alternating the two types.
- Competitor AI (UpdateCompetitorAI 0x4256c0, speed in global 0x8015e0): between x 4480 and 6400, while not
  more than 100 px ahead → +10 ("Giving Barbie a run…"). After x 9600, if Barbie's speed > 220 and the rival is
  within 100 px behind her or ahead → −10 down to the min ("Let Barbie win"). Equal speed for > 1.5 s → ±10.
  More than 100 px behind (and below max) → +10 (+20 if more than 250 px behind). More than 100 px ahead and
  above min → −10. It stops at x > 11900 (gait 2), beyond 12160 while Barbie is still < 11520, or beyond 12480.
- Finish: after x 11520 the horses slow and stop (sequence 3, "stops"/"cstops"). When both velocities reach 0
  → finished (+0x238=1). If the cursor anim is hidden, app+0x188->vt+0x14(course 1 ? 13 : 3) and 0x41d860.
  3 s later: toolbar state calls 0x41c190/0x41c1f0, the cursor is shown again, and PlayResultComment 0x425f90
  runs (random db0489/91/53/54, db0492, db0498/0501 or db0508/0511, db0490, dc0497/0499 depending on the
  rival), followed by DelayVoice("db9059", 5000).

| addr | name | purpose |
|---|---|---|
| 0x422d00 | ctor | above |
| 0x423cc0 | dtor | Releases the sounds. **Result: winner == Barbie → app+0x304=1, app+0x24C += 1 ("Barbie Won!"). Winner == rival → app+0x304=2, app+0x250 += 1. Then SaveGame(app+0x1B0, slot)**. Closes the horse omt, restores the time mode and cursor, ~HCToolbarNode |
| 0x424000 | KeyDown | key 1 → ShowExitDialog(1) (unless the race is running). Keyboard mode: key 8/9 → +0x1C0/+0x1C4 = 0. Otherwise the toolbar KeyDown |
| 0x424080 | KeyUp | keyboard mode, race started, not finished, intro done: key 3 → jump, key 8/9 → released. Otherwise HCNode 0x454c70 |
| 0x424100 | ListenToMessage | 0x32CC (right-edge click) → **app+0x33E=1, app+0x340=finished**, GotoNode(course 1 ? "rabbitE" : "beachE"). −104 (key down) → KeyDown. −103/−102 (mouse up/down) in mouse mode while racing → input flags and double-click jump. Otherwise the toolbar handler |
| 0x4242f0 | MouseDown | start flag (10000) → started; mouse mode clears +0x1C0; deletes the flag rollover. Help (0x32CE) → voice db02001 (finished) or db04089 |
| 0x424390 | SpendTime | race loop above |
| 0x425240 | UpdateObstacles | obstacle advance, jump and refusal logic |
| 0x4256c0 | UpdateCompetitorAI | rubber-band AI |
| 0x425da0 | CheckHorseTired | 10 s at speed 300 → tired for 5 s (statics 0x80a134/0x80a138/0x80a114) |
| 0x425eb0 | SpeedStepTimer | returns 1 every 2 s |
| 0x425f00 | TryStartJump(horse) | runs raceJump or raceCJump if not already running |
| 0x425f90 | PlayResultComment | end-of-race voice |
| 0x426140 | ScriptCommand | "initRace" / "initCRace" set the script vars; "startUp" swaps startBarbie for the rider and starts "trotup" |
| 0x4263e0 | ScriptEnded | raceStart → raceRun, speed 50. raceJump/raceCJump → run again. raceAbortGo → speed 300. raceCStart → raceCRun, rival velocity 220/185/150 at random |
| 0x4266b0 | AnimFrameCue | Rider seq 1 frame 3/5 → speed 25/50. Seq 6 frame 5 → "skid", frame 9 → stop. Seq 7 frame 17 → "newjump", speed 300. Rival seq 1 frame 5/10 → velocity 50/100 |
| 0x426940 | SetBarbieSpeed(v) | viewport (+0x94) scroll velocity and horse velocity |
| 0x426980 | SetBarbieHorseVelocity(v) | horse only |

## FinalPayoff — factory "finalpayoff"

Full-screen ending movie `smk\mhp.smk`; any mouse-up or the movie end returns to the tack room.

- Base HCToolbarNode. Factory `new(0x1c4)`. ctor 0x411810, dtor 0x411980, scalar deleting dtor 0x411960.
- Vtables: main 0x7f11e8 (only +0x00 overridden); +0xC 0x7f11e0 [0x411b90, SpendTime 0x411a40];
  +0x18 0x7f11d0 [ListenToMessage 0x411b50, 0x411ba0]; +0x6C 0x7f11c8 [0x411bb0, 0x403f00]; +0x7C 0x7f11b8 [0x411bc0,…].
- Helper `FinalPayoffBackground` (new(0x14), vtable 0x7f11a0, derives OMT class ctor 0x46b830/vtable 0x7f5220 which
  holds an RGB colour (+0xC/+0xE/+0x10 u16 = 0xFFFF) and registers itself with the world via world vt+0x24);
  scalar deleting dtor 0x411930, dtor 0x411950. *(inferred: solid background behind the movie)*

| off | type | meaning |
|---|---|---|
| +0xC4 | u16 | edge exits disabled (0) |
| +0x1C0 | HCMovie* | the mhp movie |

| addr | name | purpose |
|---|---|---|
| 0x411810 | FinalPayoff::FinalPayoff | background helper, world slot+0x44(2), hide cursor (OMediaWinScreenDevice vt+0x14(0)), SetInterval(100), SetTimeMode(0), [0x809b00]=1, app+0x2a4=1, SaveGame, register listener |
| 0x411960 / 0x411980 | dtor | SetTimeMode(2), slot+0x44(3), show cursor, stop+hide+delete movie, toolbar dtor |
| 0x411a40 | SpendTime (+0xC) | first tick: load/play smk\mhp.smk at (64,48,9000); when finished → GotoNode("tackroom") |
| 0x411b50 | ListenToMessage (+0x18) | message −0x67 (any mouse-up) → GotoNode("tackroom") |
| 0x411930 / 0x411950 | FinalPayoffBackground dtors | |
| 0x411b90/0x411ba0/0x411bb0/0x411bc0 | thunk dtors | → 0x411960 |

Transitions: movie end / click → `tackroom`. Save: ctor (after app+0x2a4=1).

## Credits — factory "credits"

Plays the credits script with streamed `credits` audio; click/any key advances a page, Esc/exit button ends;
when the script ends the game quits through the toolbar QuitGame path.

- Base HCToolbarNode. Factory `new(0x1c8)`. ctor 0x410d20, dtor 0x410e40, scalar deleting dtor 0x410e20.
- Vtables: main 0x7f1060 (overrides +0x00, +0x54 MouseDown 0x410ec0, +0x6C KeyDown 0x410f20, +0x90 ScriptEnded
  0x410f70); +0xC 0x7f1058 [0x410fa0, toolbar SpendTime]; +0x18 0x7f1048 [toolbar ListenToMessage, 0x410fb0];
  +0x6C 0x7f1040 [0x410fc0, 0x403f00]; +0x7C 0x7f1030 [0x410fd0, …].
- Assets: cursor `cursorIn`; small button helper (0x40ec40, 200 bytes) created with name `exit`; sound `credits`
  (streamed, channel −1); script `credits` (variables `nextPage`, `done`).

| off | type | meaning |
|---|---|---|
| +0x1C4 | ptr | exit button helper (0x40ec40); its anim (+0xAC) gets +0x84=0 |

| addr | name | purpose |
|---|---|---|
| 0x410d20 | Credits::Credits | cursorIn, create exit button, play `credits` sound, RunScript("credits") |
| 0x410e20 | `scalar deleting destructor'` | |
| 0x410e40 | ~Credits | delete exit button, toolbar dtor |
| 0x410ec0 | MouseDown | toolbar MouseDown; exit button → SetScriptVar(credits, done, 1) else (credits, nextPage, 1) |
| 0x410f20 | KeyDown | key code 1 → done=1; other keys → nextPage=1 + toolbar KeyDown |
| 0x410f70 | ScriptEnded | toolbar ScriptEnded; "credits" → vt+0xCC QuitGame() |
| 0x410fa0/0x410fb0/0x410fc0/0x410fd0 | thunk dtors | −0xC/−0x18/−0x6C/−0x7C → 0x410e20 |

Transitions: end of `credits` script → QuitGame (desktop horse, save if app+0x34a, quit).

## Open questions

- Engine key codes (1, 3, 6–9) used by Race, TrailNode and the toolbar come from the HC key mapping (`0x45e0d0`);
  their names here are inferred from behaviour (1 = Escape, 3 = jump, 6/7 = steer, 8/9 = faster/slower).
- What the script `bridgeFrog` does (it is expected to lead to the leapfrog node) and how the yard `Intro` script
  ends: both live in script data (see [scripts.md](scripts.md) and the `.omt` SCR chunks), not in code.
- BellGame objects 4–8 are handled in MouseDown and deleted in the dtor but never created by the node; the
  `GameAmb` script presumably creates them.
- Voice-variant flags rec+0xEC/+0xEE/+0xF0 (app+0x29C/0x29E/0x2A0) are read by mission code but no writer exists,
  so they are always 0 in this executable.
- M1 quirk: leaving the beachE locket stop with the locket unopened clears the mission without crediting it
  (`0x440870`). Pager quirk: answer table entry [3] can never play. Saddlebag quirk: removing a picture item
  clears the wrong slot array. Map quirk: Close releases sound "trailnode" instead of "trailsign". These are kept
  as observed behaviour of the original.
- Godiva::Start calls QuitGame(1) when the disc check fails, before the player record is initialised, so it
  would save an uninitialised record (the slot global is still 0).
