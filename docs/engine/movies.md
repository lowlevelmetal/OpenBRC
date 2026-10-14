# Movies (Smacker players) and the trail-ride video

The game plays Smacker 3.2f video (`smackw32.dll`) through four players. This document covers the
three movie players, the intro/performance test and the trail-ride video pipeline. Sprite
animations decoded from embedded SMK2/SMAK chunks are in `animation.md` §6 and
`../formats/hcanim.md`. The Smacker API calls themselves (flags, struct offsets, blitter semantics)
are in BarbieRecomp `docs/api_usage.md` §6; which Smacker features the data uses is in
`../formats/smacker-usage.md`.

| Player | Constructor / vtable / size | Used for |
|---|---|---|
| **HCMovie** (`E:\Projects\Omt\Godiva\HCMovie.cpp`) | `0x453240(owner)`, `0x7f4520`, 0x124 | Intro, MHP (final payoff), Carousel, OpenDoor (carousel node), BellMH1–3 / MH7 (bell game), Drink* (trail) |
| **SmackMovie** (no source string; between HCLogoNode.cpp and the node base) | `0x41a300(fps, usePalette)`, `0x7f1918`, 0x114 | OpenDoor in the opendoor node, FeedCloseUp* in the tack room |
| **SmackOverlayMovie** (derived from SmackMovie) | `0x41ab50(fps, double)`, `0x7f1aa0`, 0x130 | trail-ride segments, back loops, horse heads, deer, butterflies, trail overlays |
| HC2DAnim | `0x4485b0` loader | sprites (SMK2/SMAK chunks), random frame access |

All three movie players are OMT elements: after opening, the owner calls element vtable +4
`Place(x, y, z)` (floats; `0x467540`), +0x58 `Link` (`0x4676d0`, inserts it into the node's element
tree), and clears the "paused" field to start playback. z is the depth key the renderer sorts on
(values used: 2000, 4000, 5000, 9000, 9999, 40000, 100).

Global Smacker setup: `SmackSoundUseMSS(HDIGDRIVER)` once at startup (`0x44ad35`), only if Miles
started; no `SmackVolumePan`/`SmackSoundOnOff`. Every open requests all audio tracks
(`SMACKTRACKS` = 0xFE000), so files with audio play it through the Miles driver; movie audio is not
under the sound manager's control (no channel, no SoundDone) and is affected by the master volume
*(inferred)*.

---

## 1. HCMovie

### 1.1 Fields

| Off | Type | Meaning |
|---|---|---|
| +0xDC | Node* | owner (`param+0x90` of the constructor argument) |
| +0xE0 | Smack* | Smacker handle |
| +0xE4 / +0xE8 | char* / u16 | file name as given (e.g. `"smk\Intro.smk"`) |
| +0xEC | int | opened (1 after a successful Open) |
| +0xF0 | int | **paused**: 1 after construction; callers set 0 to start |
| +0xF4 | int | loop (0 everywhere) |
| +0xF8 | int | **done**: set when the last frame has been shown and loop is 0 |
| +0xFC | int | use the SMK palette (1) |
| +0x100 | int | double size (always 0 in practice) |
| +0x104 | int | direct to screen (forced 0) |
| +0x108 | int | DirectDraw display active |
| +0x10C | int | colour-key flag (see Open) |
| +0x110 / +0x112 | s16 | world width / height |
| +0x114 | u8* | decode buffer (the world's pixels) |
| +0x118 | u8* | half-size buffer for double mode |
| +0x11C | HSMACKBLIT | `SmackBlitOpen(2)` for double mode |
| +0x120 | renderer* | owner's view renderer; `+0x1C == 3` = dirty-rectangle mode |

### 1.2 Open (`0x4534e0`, vtable +0xA0)

`Open(name, preload, double, directToScreen, noSkip, extraBuf, colorKey)` → 1 / 0.

1. Copy the name; find the file with `0x44d2a0`: HD path + name, then CD path + name (`OpenFile
   OF_EXIST`). Not found → `"Error loading movie %s: couldn't find!"`. Names are relative to the
   Resources folder: `"smk\Intro.smk"` or `"DrinkPal.smk"` (Drink files live in Resources itself).
   File lookup must be case-insensitive (`smk\BellMH3.smk` vs `SMK/BellMh3.smk`, `smk\mhp.smk`).
2. Read width/height with the CRT header probe `0x452f60` (accepts `SMK2`; `SMKa`/`SMKb` give a
   zeroed result). Failure → `"Error getting Smacker info for movie %s!"`.
3. World size = movie size (×2 each way if `double`, plus `SmackBlitOpen(2)`).
4. `directToScreen` is logged as ignored (`"DirectToScreen flag ignored … it doesn't work."`) and
   cleared.
5. Create an offscreen 8-bit world of that size (DirectDraw surface world `0x473be0` or GDI world
   `0x458390`) with **colour key RGB(255,0,255)** when `colorKey` ≠ 0 (all callers pass 1). Entry 254
   is magenta in the BellMH/MH/Carousel/OpenDoor/Drink palettes but not in Intro and MHP, so
   *(inferred)* index-254 pixels are transparent in the former and nothing is in the latter. If the display is not 8-bit, the world gets the global palette
   `[0x80a3cc]`. Wrap it in a canvas and attach it to the element.
6. `SmackOpen(path, 0xFE000 | (preload ? SMACKPRELOADALL 0x200 : 0) | (noSkip ? SMACKNOSKIP 0x400 : 0)
   | (extraBuf ? SMACKLOADEXTRA 0x100 : 0), extraBuf ? extraBuf : -1)`.
7. opened = 1, done = 0; lock the world, `SmackToBuffer(smk, 0, 0, W, H, pixels, 0)` (pitch =
   width; double mode decodes into a separate half-size buffer), **show the first frame immediately**
   (Advance), unlock.

### 1.3 Frame tick and advance

* Tick `0x453490` (element idle interface, every main-loop pass): if opened and not paused and
  `SmackWait(smk) == 0` → Advance. HCMovie has **no timer of its own**: the file's frame rate (via
  SmackWait, which follows the Miles audio clock for files with audio *(inferred)*) paces it. All
  HCMovie files are 10 fps except Drink* (15 fps).
* Advance `0x453a20` (vtable +0xA4), only if loop or not done:
  1. Palette: if `smk->NewPalette` and use-palette: convert the 256 RGB entries to 16-bit
     (`v·65535/255`) into an OMT palette. If DirectDraw is active **and** the movie is exactly
     640×480 → set it as the **screen palette** (`0x44e910` + display palette set); otherwise attach it
     to the movie's world (world vtable +0x70, which broadcasts "palette changed"). Smacker sets
     NewPalette only on frames with a palette record — on the disc that is frame 0 only.
  2. Lock, `SmackDoFrame`, unlock; double mode: `SmackBlit` 2× from the half buffer.
  3. Dirty area: if the renderer is in mode 3, every rectangle from `SmackToBufferRect(smk, 0)`
     (offset by the view origin) is invalidated; otherwise the whole element is marked dirty.
  4. If `FrameNum == Frames − 1` and loop is 0: call the current node's **MovieDone(basename)**
     (node vtable +0x84, `0x456fe0`: posts **script event 0x6B** with the file name after the last
     `\`, e.g. `"Intro.smk"`) and set done = 1 — the last frame stays on screen. Otherwise
     `SmackNextFrame` (a looping movie wraps to frame 0 inside Smacker).
* Destructor `0x453370`: SmackBlitClose, SmackSummary (discarded), SmackClose, release the world.

### 1.4 HCMovie uses

| Node / function | File | Open args (preload, dbl, direct, noSkip, extra, key) | Place (x, y, z) | Ends |
|---|---|---|---|---|
| IntroMovie `0x41f0d0` | smk\Intro.smk (512×384, stereo audio, 42 s) | 0,0,0,1,0,1 | (64, 48, 9000) — centred | §2 |
| final payoff `0x411a40` | smk\mhp.smk (512×384, stereo audio, 75.7 s) | 0,0,**1**,1,0,1 | (64, 48, 9000) | node idle: done → "tackroom"; mouse event −0x67 (`0x411b50`) → "tackroom" at any time |
| carousel `0x410540` | smk\Carousel.smk (640×480, silent) | 0,0,0,1,0,1 | (view x, 0, 2000) | node logic; also starts "MusicBox" on ch 13 and voice DB9045 |
| carousel `0x410be0` | smk\OpenDoor.smk (640×480, mono audio) | **1**,0,0,1,0,1 | (view x, 0, 4000) | node logic |
| bell game `0x40b7b0` | count `app+0x270`: 1 → smk\MH7.smk; >1 → BellMH1/2/3 by count mod 3 = 0/1/2 (640×480, mono audio) | 0,0,0,1,**2300000**,1 | (0, 0, 2000) | node logic |
| trail `0x443710` | DrinkPal/Wht/Pnt/Dpl.smk by horse `app+0x1C0` (640×480, 15 fps, silent) + sound "drink" | 0,0,0,**0**,0,1 | (0, 0, 9999) | node logic |

"view x" = the node view's horizontal origin (`node+0x94 → +0x40`), so the movie covers the visible
screen of a scrolling scene *(inferred)*. No movie is skippable by HCMovie itself; skipping is node
logic (intro: §2; final payoff: any click).

---

## 2. Intro movie node and the performance test (`IntroMovie.cpp`)

Node vtable `0x7f1c28`, constructor `0x41e990`:

* `fopen(HDPath + "perf.inf", "r")`: exists → `node+0x1C0` = 1 (**intro skippable**), else 0.
* Start (vtable +0x48, `0x41f0d0`): create and start the Intro HCMovie (§1.4).
* Idle (`0x41ef90`): once, records `GetTickCount()` in `[0x800da8]`; when the movie's done flag is
  set: `StopAll(purge)` and go to node **"yard"** if Godiva app `+0x1C0` (current horse) == 4, else
  **"tackroom"** *(4 = no horse chosen yet, inferred)*.
* Key (vtable +0x6C, `0x41f080`) with a non-zero character, or mouse event −0x67 (`0x41f010`):
  only if skippable → same destination immediately.

Destructor `0x41ebb0` runs the **low-end machine test** on the intro's Smacker statistics:

1. `SmackSummary(intro, &sum)`; registry values (`0x44ccc0`, the game's key, `application.md` §4.2):
   `LowEndFlag_SoundFramesToDrop` (default 0), `LowEndFlag_PercentDecompression` (default 30).
2. `perf.inf` in the HD path: if it opens (`"r"`), read 8 floats from it. Otherwise open it `"w"`,
   compute and write 8 floats (only the first 5 are set; the last 3 are uninitialised stack):

   | Float | Value |
   |---|---|
   | 0 | TotalDecompTime / TotalTime |
   | 1 | TotalBlitTime / TotalTime |
   | 2 | TotalReadTime / TotalTime |
   | 3 | TotalReadSpeed |
   | 4 | SoundSkips |

   Both opens are CRT text mode *(quirk: a 0x0A/0x1A byte in the floats would be translated)*.
3. Log `"Performance Summary - Decompression percent = %f …"`.
4. **LowEnd = (float0 · 100 > PercentDecompression) || (float4 > SoundFramesToDrop)**. Stored in
   Godiva app `+0x334` and written to registry `LowEndFlag_IsLowEndMachine`, which the exe never
   reads back. app `+0x334` is only consulted by the trail rides (§4).
5. Stop and delete the movie.

The disc ships `Resources/perf.inf` (32 bytes): 0.0879, 0.0, 0.0016, 8175260.0, 0.0 + 3 garbage
floats — the developers' machine (8.8 % decompression, no sound skips). `data1.cab` lists
`perf.inf`, so the installer probably copies it to the HD path *(inferred)*; then the intro is
skippable from the first run and the test always yields **not low-end**. With no perf.inf the
first intro must play to the end and the measurement decides.

OpenBRC: decoding is never the bottleneck — treat LowEnd as 0 unless the user asks for the low-end
look (an option could select the `_L` presentation for authenticity).

---

## 3. SmackMovie and SmackOverlayMovie

### 3.1 SmackMovie (`0x41a300`)

* All instances share **one 640×480 8-bit world** (`[0x809f50]`, canvas `[0x809f54]`, refcount
  `[0x809f58]`), created on first use (DirectDraw surface world or GDI world).
* Fields (+0xDC Smack*, +0xE0 DirectDraw active, +0xE4 path, +0xE8 opened, +0xEA paused (1),
  +0xEC loop (**1** by default), +0xEE done, +0xF0 fps (ctor arg), +0xF4 max fps (ctor arg),
  +0xF8 accelerate, +0xFA decelerate, +0xFC use-palette (ctor arg), +0x100 world pixels,
  +0x104/+0x106 world size, +0x108/+0x10A source x/y, +0x10C/+0x10E clip size (640×480),
  +0x110 centre/letterbox flag).
* Open `0x41a700(fullPath, preload)` (vtable +0xA4): path used as given (callers build CD path +
  `"smk\…"`). Flags `0xFE000 | (preload ? 0x200 : 0) | (fps ? 0x80 : 0)`; with fps ≠ 0 it first
  calls `SmackFrameRate(fps)` (forces the file's rate to fps). Decode target: the shared world at
  (0,0), pitch 640.
* Tick `0x41a640`: if opened and not paused and `GetTickCount() − [0x809f5c] > 1000 / fps` and
  `SmackWait == 0`: `[0x809f5c]` = now (**one timestamp shared by all instances**), Advance; then if
  accelerate: fps + 1, capped at 24; else if decelerate: fps − 1, and 1 becomes 2. `0x41a990` /
  `0x41a9c0` step fps down (min 2) / up (max = constructor fps).
* Advance `0x41a7e0` (vtable +0xA0): palette as HCMovie but DirectDraw → always screen palette,
  else world palette; lock, `SmackDoFrame`, unlock, mark dirty. At the last frame: loop →
  `SmackGoto(smk, 1)` (first frame), else done = 1. Otherwise `SmackNextFrame`.
* `0x41a610(n)` = `SmackGoto(smk, n)` if n < Frames, done = 0. Frame numbers in `SmackGoto` are
  1-based (BarbieRecomp §6.4).

Uses: opendoor node `0x421200`: `SmackMovie(12, 1)`, open CD path + `"smk\OpenDoor.smk"` with
preload, loop 0, place (view x, 0, 5000), `SmackGoto(1)`, unpause, `PlaySound("dooropen")` —
so OpenDoor.smk (10 fps in the file) runs at 12 fps here. Tack room `0x42f590`:
`SmackMovie(10, 1)`, `FeedCloseUp<Dap|Paint|Pal|White>.smk`, loop 0, place (view x, 0, 40000),
`SmackGoto(1)`, unpause, `PlaySound("eating")`.

### 3.2 SmackOverlayMovie (`0x41ab50(fps, double)`)

A SmackMovie (at +0x14) plus a list of overlay Smacker streams composited over it. Object layout:

| Off | Meaning |
|---|---|
| +0x04 | main decode buffer (`radmalloc(W·H)`, only when overlays exist or double) |
| +0x08 | overlay list head; node (0x1C bytes) = {Smack*, buffer, u16 enabled, next, u16 owned, x, y}; the list ends with an empty node (next = 0) |
| +0x0C | u8 transparent index |
| +0x0D..+0x0F | key RGB (default 255,255,255) used to find the transparent index |
| +0x10 / +0x11 | palette range owned by the main movie (default 0..255) |
| +0x12 | s16 horizontal scroll of the overlays |
| +0x14.. | SmackMovie fields (+0x14 added: +0xF0 Smack*, +0xF4 DD flag, +0xFC opened, +0xFE paused, +0x100 loop, +0x102 done, +0x104 fps, +0x108 max fps, +0x10C/+0x10E accel/decel, +0x110 use palette, +0x114 world pixels, +0x11C/+0x11E source x/y, +0x120/+0x122 clip w/h, +0x124 centre) |
| +0x128 | double (2×) flag |
| +0x12A | apply-palette-once flag |
| +0x12C | OMT palette in use |

* One global `HSMACKBLIT` (`[0x809f60]`, refcount `[0x809f64]`), opened with flags 1, or 3 when the
  first instance has double set.
* Open main (`0x41adb0`): as SmackMovie, then with double set redirect decoding to the private
  buffer (+0x04). AddOverlay (`0x41ae20(path)`): open with `0xFE200` (+`0x80` when fps ≠ 0), decode
  into its own radmalloc buffer, append; enabled = 0 until the owner enables it.
* Tick `0x41acb0`: like SmackMovie but with its own shared timestamp `[0x809f68]`, and it also
  requires `SmackWait == 0` for every enabled overlay.
* Advance `0x41afe0`:
  1. Palette (if main NewPalette and use-palette): entries [range low..high] from the main movie,
     entries above high from the **first overlay** (when overlays exist and low ≠ 0, or high ≠ 0);
     any entry equal to the key RGB sets the transparent index. Apply as screen (DirectDraw) or world
     palette. Trail rides disable this and set the palette and index themselves (§4).
  2. Lock the world, DoFrame main and every enabled overlay, unlock.
  3. Letterbox origin: if centre: (offX, offY) = ((640 − clipW)/2, (480 − clipH)/2), else (0,0).
  4. If overlays exist or double: blit the main buffer into the world. 1×: whole movie at (0,0).
     2×: `SmackBlitSetFlags(2)` and `SmackBlit(world, 640, offX/2, offY/2, buf, movieW, srcX,
     srcY, clipW/2, clipH/2)` — each source pixel becomes 2×2 and the destination offset is in
     half-resolution units (BarbieRecomp §6.6).
  5. Each enabled overlay with `SmackBlitTrans` (pixels equal to the transparent index are
     skipped), 1× only: 640-wide overlays are shifted horizontally by the scroll (+0x12); others are
     placed at (x + scroll, y), clipped to the screen and to the letterbox rectangle.
  6. Main: last frame → loop ? `SmackGoto(1)` : done; else NextFrame. Overlays **always loop**.
  7. If apply-palette-once: apply +0x12C (`0x41b5e0`) and clear the flag.
* Restart (`0x41afb0`: pause, main to frame 1, overlays to frame 1 via `0x41ab20`) — `0x41ab20` never
  advances its list pointer and **hangs if an overlay exists** (no direct caller found; vtable only).

---

## 4. Trail rides (TrailNode.cpp / TrailRide.cpp)

Only the video side is described here; the ride logic (steering, events, missions) is in the trail
documentation.

### 4.1 Settings decided at TrailNode construction (`0x431420`)

Unless debug mode (Godiva app `+0x336`) is on:

| Flag | Address | Normal | Low-end (app `+0x334` ≠ 0) |
|---|---|---|---|
| horse heads | `[0x802794]` | 1 | 1 |
| trail music | `[0x802798]` | 1 | **0** |
| ambient | `[0x80279c]` | 1 | 1 |
| dialogue | `[0x8027a0]` | 1 | 1 |
| back loops preloaded for all trails | `[0x8027a4]` | 1 | 1 |
| butterflies | `[0x8027a8]` | 1 | **0** |
| use `_L` files | `[0x80a16c]` | 0 | **1** |
| programmatic letterbox | `[0x80a170]` | 0 | **1** |
| video letterbox | `[0x80a174]` | 0 | **1** |
| frame rate | `[0x802790]` | **14** | **12** |

Video letterbox forces programmatic letterbox on. In the yard with debug mode on, keys toggle them
(`0x43d8a0`): A/a ambient, B/b back loops, D/d dialogue, F/f butterflies, H/h horse heads,
L/l tutorial, M/m music, O/o `_L` files ("NewBridge"/"OldBridge"), P/p programmatic letterbox,
V/v video letterbox (lower case = off), R/r jump to "rabbitE".

### 4.2 Main segment movie (`0x437e50`)

Trail index (`ride+0x400`): 0 aaW, 1 cwL, 2 obS, 3 rfS, 4 rrE, 5 trE, 6 wwE, 7 wwW (prefix table
`0x802f48`). Main file = table `0x802f68`[trail·2 + `[0x80a16c]`]:

| Trail | Full (400×300, 10 fps in file) | `_L` (300×152) | Frames |
|---|---|---|---|
| 0 | AppleW.smk | AppleW_L.smk | 831 |
| 1 | CottageW.smk | CottageW_L.smk | 199 |
| 2 | Bridge.smk | Bridge_L.smk | 293 |
| 3 | Rainbow.smk | Rainbow_L.smk | 420 |
| 4 | RabbitE.smk | RabbitE_L.smk | 1132 / 1131 |
| 5 | TurtleE.smk | TurtleE_L.smk | 608 |
| 6 | WishingN.smk | WishingN_L.smk | 196 |
| 7 | WishingS.smk | WishingS_L.smk | 204 |

Setup: `SmackOverlayMovie([0x802790] fps, double = 1)`; transparent index 0xFE; **use-palette 0**
(SMK palettes ignored — the `_L` palettes even have entries 254/255 black); palette = the trail's
`DATA` palette (`0x431200`: 768-byte RGB `DATA` chunk by name, white if missing, entries 0xC0..0xFF
then copied from the shared sprite palette by `0x431380`); apply-once; loop 0.

* Normal presentation (no letterbox): source origin = (`ride+0x328`, `ride+0x32A`), overlay scroll =
  (`ride+0x328` − 40)·3, clip 640×480: the 400×300 movie is shown doubled (800×600) through a
  640×480 window panned by the ride (centre pan x = 40 → scroll 0).
* Letterbox (`[0x80a170]`): clip 600×300, centred, `_L` file forced (`[0x80a16c]` = 1): the 300×152
  movie is doubled to 600×304 and its top 300 lines shown at (20, 90); black above and below. With
  video letterbox the source origin stays (0, 0); with the programmatic letterbox alone (debug key P)
  the ride's pan offsets are still applied to the small movie *(inferred: a developer-only mode)*.

So **`_L` = the pre-cropped, half-resolution letterbox version** used on low-end machines: about 70 %
of the full file's size, 300×152 instead of 400×300, the same frame count (−1 for the back loops and
RabbitE).

### 4.3 Other trail Smacker streams

| Stream | Name construction | Notes |
|---|---|---|
| Back loops | `"smk\" + prefix + " back" + {0,1,2} + ("_L") + ".smk"` (`0x438930`) | 9 frames (8 for `_L`); with `[0x8027a4]` = 1 the loops of trails 0, 3, 4, 5 (aaW, rfS, rrE, trE — exactly the files on disc; the loop runs over prefixes 0..5 and skips 1 and 2) are all preloaded, else only the current trail's (never for trails 1 and 2). Each is a separate SmackOverlayMovie at the same fps, letterboxed like the main movie, with its own horse-head overlay. |
| Horse heads | table `0x802fd8`: [move][horse·2 + `[0x80a170]`], moves straight/left/right/jump/rev, horse 0 pal, 1 wht, 2 pnt, 3 dpl; files `smk\HorseHeads\<h>_<move>_crop.smk` or `<h>_<move>_L.smk` (`fwd_L` for straight) | overlay on the main movie at x = 199, y = 480 − height (390 − height when letterboxed, i.e. bottom of the 600×300 box); `_crop` heads are 300..412 wide × 332..428 high, `_L` 300..412 × 244..340 |
| deer.smk | trail 0 only, not letterboxed, not low-end | 464×164, 15 fps, overlay at y = 90 |
| Butterflies | `"smk\butterflies\" + trail prefix (table 0x8030c0) + species (3 per trail, table 0x8030e0) + size suffix (table 0x803140: _20/_50/_80) + optional "REV"` (`0x439760`) | 108 files, 2 frames each; off in letterbox mode |
| Event overlays | prefix + " " + {HorseShoe, Branches, FallenBird, Cottage, HoofPrints, Sparkle} + ("_L") + ".smk" (names at `0x8036bc`…; construction *inferred* from the strings and file names) | 400×300 / 300×152, 21..87 frames |

The `TRAIL :: smack summary` lines (`0x431fa0` TrailNode, `0x437a40` TrailRide) only log the
SmackSummary of the main movie (frames per second = TotalFrames·1000/TotalTime, etc.); nothing
adapts to them.

### 4.4 Unused movie files *(inferred: no reference in the exe, scripts or data)*

`SMK/MH1.smk`–`MH6.smk`, `ApplePickAnim.smk`, `aaW AppleGame.smk`, and the 20 half-size horse heads
`HorseHeads/*_CropHalf.smk` / `Wht_Jump_Crophalf.smk`. (MH7, BellMH1–3, the `_crop` and `_L` heads
and the event overlays are used.)

---

## 5. Open questions

* How a world palette (non-640×480 HCMovies such as the intro) reaches the 8-bit display — renderer
  docs.
* The exact meaning of the depth values and of HCMovie field +0x80 set by the intro.
* Whether the installer really copies perf.inf (InstallShield cabinet not unpacked).
