# Display: screen set-up, frame composition, transitions and the palette

How a frame reaches the screen in the DirectDraw configuration the game uses by default (640×480,
8 bits per pixel, exclusive full screen), how nodes compose their frames from a background canvas and
depth-sorted anims, how node transitions fade, and how palettes flow from resources to the hardware.

Related documents: [application.md](application.md) (scheduler, world clock, node switching),
[input.md](input.md) (cursor), [animation.md](animation.md) (drawing and hit-testing one anim),
[movies.md](movies.md) (Smacker players, movie palettes, letterboxing), `docs/formats/canvas.md` and
`docs/formats/blit.md` (image formats). BarbieRecomp `docs/api_usage.md` §3 and §7 list every DirectDraw
and GDI call with its arguments; this file explains the engine logic that drives them.

Addresses are virtual addresses in `Barbie Riding Club.exe`. *(inferred)* marks conclusions not proven
instruction by instruction.

## 1. Objects

| Object | Where | Class / constructor | Role |
|---|---|---|---|
| Screen device | `[0x80ea40]` (getter `0x46c320`) | GDI `OMediaWinScreenDevice` `0x46ba40` (vtable `0x7f5250`), replaced at Init by `OMediaDXScreenDevice` `0x49e3e0` (vtable `0x7f63e0`) | display mode, palette driver, screen world, offscreen world factory |
| Screen world | device +0x10 | `OMediaDXSurfaceWorld` `0x473be0(isScreen=1)` | primary surface + attached back buffer (flip chain), 640×480×8 |
| Display object | `app+0x3c` | `0x47af10` (0x68 bytes, vtable `0x7f5b00`) | the window; top-level message target `[0x80ea60]` |
| Window port world | display +0x14 | `OMediaWinWindowPortWorld` (vtable `0x7f5a48`) | forwards draw, fill, blit and swap calls to the screen world when one exists (`0x4a0ee0`, `0x4a0c60`, `0x4a0cc0`, `0x4a0cf0`); GDI drawing into the window otherwise |
| Screen port | display +0x5c | `0x47b4b0` (0x44 bytes, vtable `0x7f5b28`; base `0x45f6a0`) | the render target every node view draws into: +0x24 = window port world, +0x2c = offscreen "back page" (device vt+0x34) |
| Node (world) | `app+0xec` | `HCNode` `0x454320` on `OMediaWorld` `0x45c710`/`0x45c900` | element list, background, views, clock |
| View | node +0x94 | `0x492ed0` (0xc4 bytes, vtable `0x7f5de8`) | rectangle on the port, camera position, renderer |
| Renderer | view +0xa8 | `0x494a30` (0x38 bytes, vtable `0x7f6030`) | draws the world into the port, dirty rectangles |
| Background | node +0x68 | `0x493600` (canvas background, vtable `0x7f5e18`); the world constructor first installs a plain colour background `0x46b830` | fills the view before the elements |

Device methods used by the engine (DirectDraw / GDI implementations):

| Slot | DirectDraw | GDI | Meaning |
|---|---|---|---|
| vt+0x10 | `0x49e890` | `0x46cb40` | palette driver (DirectDraw: created on first use) |
| vt+0x14 | `0x49e9c0` | `0x46bde0` | set **present mode** (+0x1c; DirectDraw also marks the screen world dirty, +0x5e = 1) |
| vt+0x18 | `0x49ec70` | `0x46cc70` | set display mode |
| vt+0x24/+0x28 | `0x49f310`/`0x49f320` | `0x46c470`/`0x46c480` | width / height |
| vt+0x34 | `0x49f4f0` | `0x46bf80` | new offscreen world (DirectDraw surface world / OMT memory world) |
| vt+0x3c | `0x49f450` | `0x46bdf0` | screen world (created on demand; **non-zero exactly when DirectDraw is in use**) |
| vt+0x54 | `0x49e9e0` | – | `IDirectDrawPalette` (created from the system palette on first use) |

## 2. Screen configuration

* Mode: Init's `0x44ba00` picks 640×480×8 from the enumerated modes and calls device vt+0x18, which
  re-creates the DirectDraw object, sets `DDSCL_FULLSCREEN|DDSCL_EXCLUSIVE`, `SetDisplayMode(640,480,8)`
  and builds the screen world with one back buffer (api_usage §3.3–3.6). It then busy-waits 1000 ms.
* The window is a topmost borderless popup at (0, 0) of the display size.
* Each `HCNode` places its view rectangle by centring 640×480 on the display and rounding the left edge
  toward zero to a multiple of 64 (`0x454320`): `left = W/2 − 320; left −= left mod 64` (C remainder),
  `top = H/2 − 240`. At 640×480 this is (0, 0, 640, 480).
* Draw target (`0x47bd70`, port vt+0x28): if the window port world has a separate back buffer (screen
  world vt+0x78, `0x475990`: back surface ≠ front surface) the views draw **directly into the
  DirectDraw back buffer**; otherwise (GDI) into the offscreen back page.

## 3. One frame

A frame is produced inside the node's world task (application.md §6.2–6.3) when TimeToUpdate says so.
`HCNode::UpdateFrame` (`0x456d50`) returns immediately while the modal flag `[0x80a438]` is set,
refreshes the debug overlay if present, then runs `OMediaWorld::UpdateFrame(force)` (`0x45cff0`):

1. If `force`, set world +0x4c (full redraw).
2. Prepare each view (view vt+0x14, `0x4932a0`): clamp its rectangle to the port and to the renderer's
   limits, and advance the view camera's motion.
3. If the world is not paused:
   1. **Update elements** (vt+0x38, `0x45d080`): for every element in list order call its update
      (vt+0x20; anims advance by the clamped frame delta, animation.md §5.3), recurse into children,
      and delete elements flagged for deletion (+0x64).
   2. **Sort** (vt+0x20, `0x466ff0` on the sorter at world +0x6c): only if the list is marked unsorted
      (+0x74, set when an element's z changes). Every `HCNode` uses sort mode 3 (+0x78): a repeated
      bubble pass that swaps neighbours when `next.z < current.z` (float compare, strict), until no
      swap happens. The list ends up in ascending z; equal z keep their relative order. Modes 1 and 2
      (by x or y, optionally plus width/height, `+0x76`) are not used by the game.
4. If the world is not paused, or `force` is set: **render** every view (view vt+0x18 → renderer
   vt+0x04, §3.1), then **present** (vt+0x40, `0x45d0e0`): each queued refresh request calls the
   port's Present (vt+0x44, §4) with its rectangle and dirty list.
5. Clear +0x4c.

A paused world (pause count > 0) draws nothing unless forced; a forced redraw of a paused world shows
the elements without advancing them.

### 3.1 Renderer (`0x494b30`)

Fields: +0x04 world, +0x08 view, +0x14 full-redraw flag (u16), +0x18/+0x1a index (0/1) of the
current and the other dirty list (u16), +0x1c mode, +0x20 and +0x2c two dirty-rectangle lists. The
constructor sets mode 1; the game-node base sets **mode 3** (`0x41b690`) and world vt+0x44 (`0x45c890`)
changes it for all views (forcing a full redraw when it changes). The movie nodes switch to mode 2 or 0
and back to 3 (§4.1).

Per render:

1. `draw = port->GetDrawWorld()`; remember the mode.
2. If the mode is not 0 and the port is a double-buffered screen: when the device presents by copying
   (present mode +0x1c, or the system-memory back page flag +0x38) a full redraw is forced if the screen
   world reports itself dirty (vt+0x84, +0x5e); when the device **flips**, this frame is rendered in
   mode 0 (everything, every frame), because after a flip the back buffer holds an older frame
   *(reason inferred)*.
3. Spin until the previous flip has completed (draw vt+0x80, `GetFlipStatus`).
4. Lock the draw world; a forced world (+0x4c) means full redraw.
5. Modes 0/1 → pass A (`0x494c10`); modes 2/3 → pass B (`0x494f90`).
6. Unlock; restore the mode.

**Pass A** (modes 0, 1): background over the whole view (full) or over last frame's element
rectangles (mode 1), then **every** element is drawn; each drawn rectangle is recorded in the dirty
list(s) for the next frame; a present request is queued with the dirty list (or none = whole view).

**Pass B** (modes 2, 3; the normal case):

1. Unless full: build the dirty list from the elements that changed since the last frame (element
   +0x7c "changed" flag; `0x495480`): the old rectangle (+0x88..+0x94) and the new one (vt+0x68),
   clipped to the view and to the element's clip rectangle. A second pass then widens the set:
   mode 2 with `0x495740`, mode 3 with `0x495920`, which lets each element contribute its own region
   object (element +0xb8). Together with the decoder rectangles that full-frame Smacker anims add in
   mode 3 (animation.md §6.2), a full-screen movie at (0, 0) repaints only what changed *(inferred;
   the merge rules were not traced in detail)*.
2. Background: `background->Draw(port, viewRect, full ? NULL : dirtyList, view)`.
3. Elements (`0x4952e0`): drawn in list order (ascending z), each clipped to the dirty rectangles it
   intersects; unaffected elements are not touched.
4. Queue the present request with the dirty list (or the whole view when full).

**Element order and children** (`0x494db0`, `0x4952e0`): the world's element list is walked in order.
An element with children draws, in this order, its children with z < 0, itself, then its children with
z ≥ 0 (child z is relative ordering among siblings; the child lists are sorted the same way). Hidden
elements (+0x48 ≠ 0) are skipped. How a single anim draws (screen rectangle = position + frame offset −
camera, colour key 0xFE/magenta, clipping) is in animation.md §7.

**Equivalence**: the dirty-rectangle passes only skip work; every pixel they leave alone holds what a
full redraw would produce, because nothing else draws into the back buffer between frames except the
transitions of §6, which are followed by a forced full redraw. A reimplementation may redraw the whole
view every frame (background, then all visible elements in ascending z) and present the whole screen
*(inferred; validate visually against BarbieRecomp)*.

### 3.2 Background canvas

`HCNode`'s constructor takes the palette from the `Canv` chunk named `"<node name> background"` and
creates the background from the image of the same name (`0x454ad0` → `0x493600` + `0x44dd90`, which
prefers a `BLIT` chunk over a `Canv`; canvas.md §7) and installs it with SetBackground (`0x45cd30`, world +0x68,
which also forces a full redraw). Its draw (`0x493770`) loads the canvas on first use, locks the canvas
and the target, and copies the canvas area under each dirty rectangle (or the whole view) to the target
**opaquely**, offset by the view camera (view +0x40/+0x44, truncated): wider panorama canvases scroll
when the camera moves (input.md §8.2). All 27 node names have such a canvas (application.md §3.1);
42 canvases on the disc are exactly 640×480×8.

### 3.3 What is on screen, back to front

1. Background canvas of the node (or the colour background if a node never sets one).
2. All anims linked to the node, by ascending z: scenery 0–101, buttons 110–400, toolbar 3000, saddle
   bag 4500, cursors 5000, a few overlays 6000–30000 (docs/formats/hcanim.md §2.1). Movies are
   elements too (`HCMovie` `0x453240` and the Godiva Smacker players derive from the OMT element class
   `0x46d800`; movies.md).
3. Debug overlay text at depth 1e10 when enabled (application.md §8.1).

There is no separate cursor or HUD layer: the cursor is the anim at z 5000 (input.md §8.2).

## 4. Presentation

The screen port's Present (`0x47ba10`, called with the request's rectangle and dirty list):

| Situation | Action |
|---|---|
| DirectDraw, present mode "copy" (device +0x1c ≠ 0) or system-memory back page | screen world vt+0x88 (`0x475b10`): `BltFast` each dirty rectangle (clipped to 640×480) from the back buffer to the front buffer; the whole screen if no list. Skipped while the app is inactive. |
| DirectDraw, present mode "flip" | screen world vt+0x7c(0) (`0x4759b0`): `Flip`, no wait |
| GDI | copy the dirty rectangles of the back page into the window (`BitBlt`) |

`0x4759b0` with wait = 1 (used by transitions) flips, or copies the whole back buffer when the back
buffer is a separate surface, then spins on `GetFlipStatus`.

### 4.1 Present mode by node

Init sets **copy** mode (`0x44aa30`: device vt+0x14(1)). Nodes that show full-screen movies switch to
**flip** mode in their constructor and back in their destructor:

| Node / function | Start | End |
|---|---|---|
| finalpayoff | constructor `0x411810`: flip; world mode 0, interval 100 ms; renderer mode 2 | destructor `0x411980`: copy; world mode 2; renderer mode 3 |
| intromovie | constructor `0x41e990`: flip; world mode 0, interval 100 ms; renderer mode 2 | destructor `0x41ebb0`: copy; world mode 2; renderer mode 3 |
| race | constructor `0x422d00`: flip | destructor `0x423cc0`: copy; world mode 2 |
| trail ride movie | `0x432c90`: flip; renderer mode 0 | `0x432f30`: copy; renderer mode 3; also `0x431fa0`: copy; renderer mode 3 |

In flip mode the renderer draws everything every frame (§3.1 step 2), so the visible result is the same.

## 5. Coordinates

* Game coordinates are 640×480 with (0, 0) at the top left of the view, which is the top left of the
  screen in the only mode the game uses.
* An element at (x, y) with frame offset (dx, dy) draws its bitmap at
  `view.left + trunc(x − camX) + dx`, `view.top + trunc(y − camY) + dy` *(per animation.md §7)*.
* The mouse position is in screen coordinates and is used as-is (input.md §3).

## 6. Transitions

### 6.1 Node-switch fade (`0x44df20` → `0x44df60`)

`0x44df20(out, targetVolume)` uses the palette fade when a DirectDraw screen exists, else the GDI
variant (§6.3). Parameters come from RequestNode (application.md §3.2): `steps = app+0xfc` (16) and the
sound flag. Nothing happens when `steps` is 0.

Set-up:

1. `vol = AIL_digital_master_volume` (0 without sound). `volStep = 0` if `vol == target`, else
   `(target − vol) / steps + 1` (C division, truncating toward zero).
2. Create three 640×480 offscreen worlds (`0x45f6a0` with world capability mask 0x22d, created by the
   gfx-world factory; DirectDraw offscreen surfaces *(inferred)*): **A** (unused), **B** (filled with RGB(0,0,0), which is index 0 in every scene palette), **C**.
3. `nodeRect` = the current node's view rectangle (view +0xac..+0xb8); `full` = (0, 0, 640, 480).
4. Fill C:
   * **Fade out**: copy the *front* buffer area `nodeRect` into C (screen world vt+0x1c with C as the
     destination): C holds what is visible now.
   * **Fade in**: restart the new node's world clock and busy-wait until it has run for
     `frameInterval + 1` ms (world +0x34, 40 for a new node), so that the next World::Idle will update;
     temporarily point the node's view at C with rectangle `full`, call World::Idle (the node updates and
     renders its first frame into C), **pause the node**, then restore the view's rectangle and target
     (the screen port).
5. Read the current hardware palette into P (palette driver GetPalette; 8-bit guns widened as v·257).
6. Find `white` = the **last** index whose colour is exactly (0xFFFF, 0xFFFF, 0xFFFF) (default 255),
   and `bright` = the first index with the smallest `3·0xFFFF − (r+g+b)` among the non-white entries
   (the brightest other colour). Replace every pixel of C equal to `white` by `bright`.
7. Faded palette F: entry 0 = black and entry 255 = white throughout; entries 1–254 are recomputed per
   step.
8. Copy the world `C` (fade out) or `B` (fade in) into the back buffer at `nodeRect`, and if the screen
   is double buffered swap with wait (`0x4759b0(1)`).

Fade out, `i` from 49 down to 0 (50 steps):

* `t0 = GetTickCount()`;
* `F[j] = P[j] · i / 50` for j = 1..254, each gun separately (unsigned, truncating);
* set F as the screen palette (§7.2: immediately visible);
* `vol += volStep`; if `vol < target` then `vol = target`; `AIL_set_digital_master_volume(clamp(vol,
  0, 127))` (only with a driver);
* spin until `GetTickCount() − t0 ≥ 12`.

Afterwards copy B (black) into the back buffer at `nodeRect` and swap with wait. The hardware palette
is left dark (entries 1–254 black). The node is deleted next (application.md §3.3).

Fade in, `i` from 0 to 49:

* as above with `F[j] = P[j] · i / 50` and the clamp `if vol > target then vol = target`;
* at `i == 0` only, after setting the palette and volume: copy C into the back buffer at `nodeRect` and
  swap with wait – the new frame appears while the palette is still black;
* spin until 12 ms have passed since `t0`.

Afterwards set P (the node's real palette) again, **unpause** the node, force a full redraw
(`node->UpdateFrame(1)`), and swap with wait if double buffered.

Notes for an implementation:

* The loops do not pump messages or run tasks; at 12 ms per step a fade takes ≥ 600 ms. Present each
  step on the host (the palette change must become visible without a flip).
* Steps 6 and the remapped C exist because entry 255 (white) is never faded: pure-white pixels would
  otherwise stay white on a black screen. The remap only affects the temporary copy C; real white
  pixels come back with the full redraw at the end.
* The sound fade is linear in steps of `volStep` and usually reaches the target long before step 50
  (127 → 0 with −6 per step takes 22 steps); see also sound.md §9.
* In the fade in, P is the palette the new node installed in its constructor (§7.2); during the fade
  out it is the palette of the old node.

### 6.2 Activation redraw (`0x44b440`)

Used for `WM_PAINT` and on `WM_ACTIVATEAPP` (application.md §5.1), and skipped while `[0x80a438]` is
set: fill the back buffer black through the window port (vt+0x24), force a full node redraw
(`node->UpdateFrame(1)`), swap with wait if double buffered.

### 6.3 GDI fade (`0x44e870`)

Without DirectDraw: if `steps ≠ 0`, fill the screen port's world black and set the master volume to the
target at once (clamped to 0…127). No palette fade.

## 7. Palette

### 7.1 Palettes in resources

Every 8-bit canvas carries a 256-entry palette with 16-bit guns (`docs/formats/canvas.md` §3). In all
scene palettes index 0 is black and 255 white (`app_validate.py`: 318 of 319 palettes; the exception is
the unused `font` canvas in `common.omt`, which is inverted), and 254 is the magenta colour key. Anim
pixels are indices into the **current game palette** (`[0x80a3cc]`); the SMK files' own palettes are
ignored for anims (animation.md §11). Movies may install their own palette (movies.md).

### 7.2 Who sets the screen palette

The screen palette is changed only through the palette driver's SetPalette (driver vt+0x04), from:

| Caller | When |
|---|---|
| `HCNode` constructor `0x454320` | the node's background canvas palette, as soon as the node is constructed (during the switch, before the fade in); also stored as the game palette `[0x80a3cc]` (`0x450180`) |
| fade `0x44df60` | each fade step and the final restore |
| Smacker players `0x41a7e0`, `0x41afe0`, `0x41b5e0`, `HCMovie` `0x453a20`, trail `0x431180` | movie palette changes, each preceded by `WaitForVerticalBlank` (`0x44e910`; a no-op while inactive because `app+0x44` is 0); details in movies.md |

There is no other palette animation: `AnimatePalette` is only used by the GDI drivers to implement
SetPalette.

### 7.3 DirectDraw palette driver (`0x4c42b0`)

The active driver with DirectDraw (`OMediaDXPaletteDriver`, created lazily by device vt+0x10):

1. Return unless the display is palettised and an `IDirectDrawPalette` exists (device vt+0x54).
2. Convert the n (256) entries: `PALETTEENTRY = (r >> 8, g >> 8, b >> 8, flags 0x40)`.
3. Unless the call's flag bit 0 is set: if n = 256 and entry 255 is black while entry 0 is not, move
   entry 0's colour to 255 and make 0 black (in both the hardware entries and, temporarily, the palette
   object); if n ≠ 256, force entry 0 to black. With the disc's palettes (0 black, 255 white) nothing
   changes.
4. `SetEntries(0, 0, n, entries)`; call the GDI base (`0x498d80`, bookkeeping only because the DirectDraw
   driver marks itself "no realise"); restore the palette object's entries 0 and n − 1.

GetPalette (`0x4c44a0`) reads the 256 hardware entries back and widens them to 16 bits as `v·257`.

At start-up, before any node palette, the DirectDraw palette is created from the Windows system palette
(device `0x49e9e0`; api_usage §3.10), so the logos' palette is the first game palette shown.

### 7.4 GDI palette drivers (only with `Use DirectDraw` = 0)

* `HCWinPaletteDriver` (`0x459aa0`, vtable `0x7f47d0`, source `HCWinPaletteDriver.cpp`), installed when
  `Use HC Palette Driver` = 1: saves 31 system colours (`GetSysColor`), switches the system palette to
  `SYSPAL_NOSTATIC`, creates a 256-entry logical palette with `PC_RESERVED|PC_NOCOLLAPSE` (5) flags,
  entry 0 black and 255 white with flags 0 (`0x459760`). SetPalette (`0x459eb0`) animates entries
  1–254 (`AnimatePalette(pal, 1, 254, …)`), re-realises, and mirrors the resulting system palette.
  The destructor restores `SYSPAL_STATIC` and the 31 system colours with `SetSysColors` – the only use of
  `SetSysColors`.
* `OMediaWinPaletteDriver` (`0x498bd0`): the OMT default (api_usage §7.2).

The HC driver reserves entries 0 and 255 exactly as the fade does, which matches the content's
convention 0 = black, 255 = white. A reimplementation renders 8-bit indices through a 256-entry RGB
table and only needs the DirectDraw behaviour of §7.3.

### 7.5 Palette-related global

`[0x805f24]` is set to 1 by `HCNode` right after installing the palette. It makes the canvas reader
(`0x471e60`) remap every canvas loaded afterwards to the display palette current at load time
(canvas.md §6); scene images share their scene's palette, so this is normally an identity copy.

## 8. Letterboxing

Letterboxing exists only in the trail rides (`TrailNode.cpp`; the yard only toggles it). The
"programmatic letter box" (`[0x80a170]`) draws the ride movie, doubled from the 300×152 `_L` file, into
a centred 600×300 window with black above and below; the "video letter box" (`[0x80a174]`) implies the
programmatic one. Both are chosen when a trail node is built: on for low-end machines (application.md
§4.3), off otherwise, and the yard developer keys can toggle them (input.md §6.4). The drawing is
documented in movies.md §4. Outside the trail rides the full 640×480 screen is always used.

## 9. Checklist for a reimplementation

1. Keep an 8-bit 640×480 back buffer and a 256-entry RGB palette; present = convert through the
   palette and show. Palette changes must be presented immediately, including inside the fade loops.
2. Per node frame: draw the background canvas at the camera offset, then every visible anim in
   ascending z (stable order for equal z), each with colour key 0xFE; present.
3. Implement the node-switch fade exactly (§6.1), including the 12 ms steps, entries 0/255 fixed,
   the white remap, the order "show black, swap, then palette up", and the forced redraw at the end.
4. A forced full redraw follows every transition and every activation.
5. Treat the present mode (copy vs flip) as an implementation detail; the image is the same.

## 10. Open questions

* The exact rectangle-merging of render modes 2/3 (`0x495740`, `0x495920`, `0x4952e0`) was only
  characterised, not specified; it is irrelevant if the whole frame is redrawn.
* The capability mask 0x22d used for the fade's scratch worlds is taken from the code; how the gfx-world
  factory maps masks to implementations (`0x465020`) was not traced.
* World A in the fade is created and destroyed without being used *(as far as traced)*.
