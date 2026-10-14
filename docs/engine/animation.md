# Sprite animations (HC2DAnim) at runtime

`HC2DAnim` (`E:\Projects\Omt\Godiva\HC2DAnim.cpp`, code `0x447a00`–`0x449f20`) is the game's sprite.
Almost everything on screen apart from backgrounds and full-screen movies is an HC2DAnim: buttons,
cursors, horses, toolbars and pagers. This document covers how one behaves at runtime. The data
formats (`HCAn`, `SMK2`/`SMAK`/`SMKI`, `.omt.cache`) are in `docs/formats/hcanim.md`.

Conventions: addresses are virtual addresses in `Barbie Riding Club.exe`; "+0x.." are byte offsets
into the object; *(inferred)* marks behaviour not directly observed in code.

## 1. Class structure

HC2DAnim is built from OMT 1.x classes. The executable has no RTTI, so the OMT class names below are
descriptive.

| Part | Offset in HC2DAnim | Constructor / vtable | Role |
|---|---|---|---|
| OMT element ("OMediaElement") | +0x00 | `0x4673d0`; main vtable `0x7f4140` (HC2DAnim) | Position, parent/layer links, visibility, pause, drawing, hit tests |
| element container part | +0x10 | vtable `0x7f4138` | child list at +0x14; `0x45da80` = find descendant by id (+0x68) |
| mover | +0x1c | vtable `0x7f4128` | constant-velocity motion (§9.3) |
| listener part | +0x6c | vtable `0x7f4120` | |
| sprite element ("OMediaAnim element") | +0x00 (derived class) | `0x4717d0`, vtable `0x7f5558` | adds the sequence player at +0xc4 and anim-aware size/position getters |
| sequence player ("OMediaAnim") | +0xc4 | `0x479dd0`; vtable `0x7f40f0` | sequences, current frame, timing (§5) |
| broadcaster ("OMediaBroadcaster") | +0xf4 | `0x45c2b0`; vtable `0x7f40e0` | broadcasts −1000/−1001 and the click message; nothing listens (§10.2) |
| HC2DAnim fields | +0x100…+0x14f | `0x447a00` | name, sequence info, Smacker state |

Object size **0x150** (allocated in `0x454ce0`). Frames are separate 0x58-byte objects (§3).

## 2. HC2DAnim object layout

| Offset | Type | Field |
|---|---|---|
| +0x000 | vtable* | `0x7f4140` (§4) |
| +0x004/+0x008/+0x00c | float | local position x, y, z (z = depth, §9.1) |
| +0x010 | vtable* | container part |
| +0x014 | list* | child elements (unused for anims) |
| +0x01c | vtable* | mover (§9.3); its data is +0x20…+0x3a |
| +0x03c | ptr | layer: the node (an OMT world) the anim is linked into. The node's timer is at layer+0x50, its sort flag at layer+0x74 and its sort mode at layer+0x78 |
| +0x040 | ptr | parent element (0 for anims) |
| +0x044 | ptr | link set by vt+0x10 (`0x468c40`) |
| +0x048 | int | hide counter; visible ⇔ this and all parents are 0 (`0x468f80`) |
| +0x04c/+0x050/+0x054 | float | absolute position = parent's absolute position + local position (`0x468020`) |
| +0x058 | ptr | this element's node in the layer's element list |
| +0x05c | int | motion pause counter |
| +0x060 | short | pass Hide/Show on to children |
| +0x062 | short | not pickable: layer picking skips the element (set for cursors by `0x41beb0`) |
| +0x064 | short | "delete me": the world deletes the element at its next update (`0x45d080`) |
| +0x068 | int | element id = the HCAn chunk id (set in `0x447cc0`) |
| +0x06c | vtable* | listener part |
| +0x074/+0x078 | short/int | own child sorting: flag = 1, mode = 3 (by z) |
| +0x07c | short | changed: needs redrawing |
| +0x080 | short | HCAn header flags & 2 (no known reader) |
| +0x082 | short | clip rectangle enabled |
| +0x084 | short | per-pixel hit test (1 by default) |
| +0x088…+0x094 | int[4] | last drawn rectangle (cleared on Link) |
| +0x0a8…+0x0b4 | int[4] | clip rectangle (left, top, right, bottom) |
| **+0x0c4** | | **sequence player** (offsets below are relative to the anim) |
| +0x0c4 | vtable* | `0x7f40f0` |
| +0x0c8/+0x0cc/+0x0d0 | vector | sequences: 8-byte {frame-list head, frame count} |
| +0x0d4 | Frame* | current frame |
| +0x0d8 | int | current sequence |
| +0x0dc | int | tick counter (frame-per-update mode) |
| +0x0e0 | int | playback pause counter; no playback while > 0 |
| +0x0e4 | short | time mode: 1 = milliseconds (HCAn flag 1, always set), 0 = one frame per update |
| +0x0e6 | short | loop (sequence flag 4) |
| +0x0e8 | short | direction: 0 forward, 1 backward |
| +0x0ea | short | started: 0 → the next update only loads the frame's duration |
| +0x0ec | short | ping-pong (never set by HC code) |
| +0x0f0 | float | milliseconds left on the current frame |
| **+0x0f4** | | **broadcaster**: vtable `0x7f40e0`, listener list at +0x0f8, count at +0x0fc |
| +0x100 | char* | name (as created, or the new name of a copy) |
| +0x104 | u16 | name length |
| +0x108/+0x10c/+0x110 | vector | per-sequence info, 8 bytes each: {u32 seqFlags, u16 z, u16 unused} |
| +0x114 | HCNode* | owning node (set with vtable +0x94, `0x449510`) |
| +0x118 | u32 | HCAn header flags |
| +0x11c | int | message broadcast when the anim is clicked; 0 = none (§8) |
| +0x120 | Smack* | Smacker handle (0 for still-image anims) |
| +0x124 | int | 1 = cropped frames (SMAK/SMK1, SMKI sizes); 0 = full frames (SMK2). The constructor sets 1. |
| +0x128 | Canvas* | 8-bit canvas that Smacker decodes into |
| +0x12c | u8* | its pixels |
| +0x130 | Canvas* | canvas the frames draw: +0x128 at 8 bpp, else a 16/32-bpp copy |
| +0x134 | void* | pixels of the 16/32-bpp copy |
| +0x138 | int | screen depth when created (8, 16 or 32) |
| +0x13c | int | pitch of +0x12c |
| +0x140 | int | pitch of +0x134 |
| +0x144 | short | canvas height |
| +0x146 | short | canvas width |
| +0x148 | short | stopped: a stop frame was reached or the sequence ended (§5.4) |
| +0x14c | ptr | node+0x94→+0xa8: the node's renderer. +0x1c is its update mode (3 = dirty rectangles); +0x18/+0x1a and +0x20 hold its two dirty-rectangle lists; +0x8 is its viewport |

## 3. Frame object (0x58 bytes)

Constructor `0x4478c0`, vtable `0x7f40a0`; bases `0x46db90` (canvas frame) and `0x479bb0` (frame).

| Offset | Type | Field |
|---|---|---|
| +0x00 | vtable* | +4 attach(player, seq) `0x479c10`; +8 detach `0x479cd0`; +0xc SetOffset(x,y,z) `0x479d30`; +0x10 SetRate(fps) `0x479d60`; +0x14 SetCanvas `0x46dc60` |
| +0x04 | ptr | owning sequence player |
| +0x08/+0x0c/+0x10 | float | offset x, y, z (from the HCAn frame-source table; z = 0) |
| +0x14 | int | updates per frame in frame-per-update mode (1) |
| +0x18 | float | duration in ms (default 40; HCAn sets it with `0x4479b0`) |
| +0x1c | float | `0x4479b0` stores duration × 0.001 here; `0x479d60` stores the fps |
| +0x20 | int | sequence index |
| +0x24 | ptr | node in the sequence's frame list |
| +0x28 | Canvas* | image (reference counted at canvas+0x14) |
| +0x2c | ptr | freed by the destructor (unused here) |
| +0x30 | ptr | collision-mask object (reference counted; never set by HC2DAnim) |
| +0x34 | u32 | HCAn frame flags (+0x200 = canvas is shared, do not release) |
| +0x38/+0x3c/+0x40/+0x44/+0x48 | char* | names for frame flags 0x20/0x40/0x80/0x100/0x10 |
| +0x4c | u16 | goto sequence (flag 8) |
| +0x4e | u16 | sequence for flag 0x20 |
| +0x50 | u16 | source index (1-based) = Smacker frame number |
| +0x52/+0x54 | u16 | content width/height from SMKI (cropped anims) |

The destructor (`0x447910`) frees the five strings. If flag 0x200 is set it clears +0x28 first, so
the shared Smacker canvas is not released.

## 4. Virtual methods

Main vtable `0x7f4140`. Entries that HC2DAnim overrides are in **bold**.

| +off | Address | Semantics |
|---|---|---|
| +0x00 | **`0x447b90`** | scalar deleting destructor (`0x447bb0`: SmackClose, release both canvases, free the sequence-info vector and the name, then the base destructors, which delete all frames) |
| +0x04 | `0x467540` | SetPosition(x, y, z) floats; marks changed; flags the layer for resorting when the coordinate it sorts by changes |
| +0x08 | `0x467600` | Move(dx, dy, dz) |
| +0x10 | `0x467750` | set parent link |
| +0x14 / +0x18 | `0x468c00` / `0x468bc0` | attach to a parent element / to a layer |
| +0x1c | `0x467760` | unlink |
| +0x20 | `0x4718e0` | per-update tick: apply motion, then run the sequence player with the world's elapsed time (§5.3) |
| +0x24/+0x28/+0x2c | `0x471a50/70/90` | absolute x/y/z **including the current frame's offset** |
| +0x30 | `0x467f70` | Hide(1) / Show(0) as a counter: Hide increments +0x48 (marks dirty on 0→1), Show decrements |
| +0x34 | `0x468f80` | IsVisible: +0x48 = 0 here and in every parent |
| +0x38 | **`0x447b50`** | Pause(on): counts +0x5c (motion) and +0xe0 (playback). Unpausing playback from 1 clears "started". HC clamps both counters at 0. |
| +0x3c | `0x468020` | recompute the absolute position (also for children) |
| +0x40 | **`0x449740`** | Pick(request): base hit test, then broadcasts +0x11c if set (§8) |
| +0x44 | `0x468240` | collides-with(element): bounding boxes, then 1-bit masks from frame+0x30 |
| +0x48 | `0x468e90` | set layer (recursive) |
| +0x4c/+0x50 | `0x468cb0`/`0x468d30` | insert into / remove from a layer's element list |
| +0x54 | `0x467710` | attach to parent and mark changed |
| +0x58 | `0x4676d0` | **Link(layer)**: add to the layer's list, mark changed, clear the last drawn rectangle, flag the layer for resorting |
| +0x5c | `0x4677a0` | GetSize(&w, &h) of the current frame's canvas |
| +0x60 | `0x467840` | screen rectangle relative to a viewport |
| +0x64 | `0x4677e0` | integer position |
| +0x68 | `0x467930` | GetRect: left = trunc(absX + frameX), top = trunc(absY + frameY), right/bottom = + canvas size |
| +0x70 | `0x4680b0` | HitTest(viewport, x, y) (§8) |
| +0x74 | `0x4679a0` | draw the current canvas into a world (§7) |
| +0x78 | `0x467b20` | draw through a viewport, clipped (§7) |
| +0x7c | `0x471a30` | current frame's canvas (frame+0x28) |
| +0x80 | `0x471b20` → `0x467da0` | add the last drawn rectangle (+0x88…+0x94) to the layer's dirty lists so that area is repainted (called by Hide) |
| +0x84/+0x88 | `0x471ab0/ad0` | local x/y + frame offset |
| +0x8c | `0x471b30` | current frame's collision mask (frame+0x30) |
| +0x90 | `0x467fc0` | pass Hide/Show to children |
| +0x94 | **`0x449510`** | SetNode(node): +0x114 = node, +0x14c = node+0x94→+0xa8 |
| +0x98 | **`0x447cc0`** | Load(manager, file, name, extStream) |
| +0x9c | **`0x448320`** | load version 1 |
| +0xa0 | **`0x4485b0`** | load version 2 |

Sequence-player vtable `0x7f40f0` (this = anim+0xc4):

| +off | Address | Semantics |
|---|---|---|
| +0x04 | **`0x449540`** | SetSequence(seq, restart) (§5.1) |
| +0x08 | **`0x449780`** | SetSequenceFrame(seq, index): SetSequence(seq, 1), then frame #index of the sequence, then process the frame (§5.5) |
| +0x0c | `0x47a080` | SetFrame(Frame*) (the frame must belong to this player) |
| +0x10 | `0x47a230` | index of the current frame within its sequence |
| +0x14 | **`0x449660`** | Advance() (§5.4) |
| +0x18 | `0x47a270` | SetTimeMode(short) |
| +0x1c | `0x47a290` | Run(elapsedMs) (§5.3) |
| +0x20 | → `0x447b50` | Pause |
| +0x24 | `0x471b10` | Invalidate: anim+0x7c = 1 |
| +0x2c | `0x471930` | FrameChanged(old): flags the layer for resorting if it sorts by x or y and the frame offset changed |

Broadcaster vtable `0x7f40e0` (this = anim+0xf4): +4 `0x45c580` Broadcast(msg, data) calls each
listener's handler (listener vtable +0) and stops early if a listener sets its +0xc flag; +8
`0x45c510` AddListener; +0xc `0x45c470` RemoveListener.

## 5. Playback

### 5.1 SetSequence(seq, restart) — `0x449540`

Ignored unless 0 ≤ seq < nSequences. Then:

1. Clear "stopped" (+0x148).
2. If hidden (+0x48 ≠ 0): set the counter to 1 and Show, so **selecting a sequence always makes the
   anim visible**.
3. Base `0x479f50`: if seq differs from the current sequence, switch to it. The new current frame
   is the first frame of seq, or, when restart = 0 and a frame is current, the frame at the same
   index in seq (its last frame if seq is shorter). Mark changed and clear "started".
4. If the anim has a node (+0x114), which it always has when created through `0x454ce0`:
   1. If restart: current frame = first frame of seq; clear "started"; mark changed.
   2. Call node vtable +0x74(anim, seq): "sequence started".
   3. Unless header flag 8: SetPosition(x, y, seqInfo[seq].z), so **each sequence sets the anim's
      depth**.
   4. loop (+0xe6) = seqFlags & 4.
   5. If there is a current frame, process it (§5.5). If it is a stop frame, call node vtable
      +0x78(anim, currentSeq) and broadcast −1000 with the anim.

### 5.2 Creation-time start

When an anim is created or fetched by name without a file (`0x454ce0`, §10.1) and is not yet linked,
it is linked into the current node, put at position (0, 0, z) and given SetSequence(0, 1). So it
shows sequence 0, frame 0 straight away. Anims preloaded from a `.omt.cache` stay unlinked, and so
invisible, until they are first requested by name. `0x41d100` (the
node helper used by game code, e.g. `GetAnim("MouthFlapStable")`) then moves it to the node's origin
(node+0x94 → +0x40/+0x44).

### 5.3 Run(elapsed) — `0x47a290`, called every world update

`0x4718e0` (element tick) first applies motion (§9.3). It then calls Run with the elapsed time of the
node's world timer (layer+0x50: last count, elapsed, initial, stopped, locked). During a world update
the timer is locked, so every element gets the same value: the milliseconds since the previous world
update, capped at the world's ceiling (500 ms, set in `0x45c900`, applied in `0x45cf60`). The world's
update rate itself is documented with the world/renderer.

Run, with `cur` = current frame and `rem` = +0xf0 (float):

```
if paused (+0xe0 != 0): return
old = cur
if cur == null: cur = first frame of the current sequence (none: return); started = 0
if time mode:
    if !started: started = 1; rem = cur.ms            // no advance on the first update
    else:
        e = float(elapsed)
        if cur.ms != 0:                              // a 0 ms frame never advances
            while e >= rem:
                e -= rem
                if Advance(): rem = cur.ms; goto done // stopped / goto / end
                rem = cur.ms
                if rem == 0: goto done
            rem -= e
else (frame-per-update mode):
    if !started: started = 1; ticks = 0
    elif ++ticks >= cur.updatesPerFrame: ticks = 0; Advance()
done:
if cur != old: mark changed (+0x7c); FrameChanged(old)
```

All anims on disc use time mode, so a frame lasts its HCAn duration. Leftover time carries over into
the next frame. Several frames can be skipped in one update when elapsed is large. **The Smacker
frame rate is never used.**

### 5.4 Advance — HC override `0x449660`, base `0x47a3e0`

```
if stopped (+0x148): return 1                       // stays on the current frame
if cur.flags & 8: SetSequence(cur.gotoSeq, 1); return 1
ended = NextFrame()                                 // base 0x47a3e0, below
stop  = !ended && ProcessFrame()                    // §5.5, for the new frame
if (ended || stop) and node: node->vt+0x78(anim, curSeq); broadcast(-1000, anim)
if ended and !stopped: Hide(); stopped = 1          // a sequence that runs out HIDES the anim
return ended || stop
```

NextFrame (`0x47a3e0`) returns 1 ("ended") without moving when the sequence has exactly one frame.
Otherwise, moving forward (+0xe8 = 0): go to the next frame; at the last frame, reverse if ping-pong,
wrap to the first frame if looping, else return 1. Backward is the mirror image. HC never sets
ping-pong or backward.

The consequences for the disc's data:

* **Stop frame (flag 2)**: the frame is shown, "sequence ended" is reported at once, and the anim
  stays visible on that frame. This is the normal end of a one-shot sequence (2107 frames).
* **No stop frame, no loop, no goto** (66 sequences, 16 of them single-frame): the last frame is
  shown for its duration, then "sequence ended" is reported and **the anim is hidden**.
* **Loop (sequence flag 4)**: plays forever; no end notification.
* **Goto (frame flag 8)**: after the frame's duration, SetSequence(gotoSeq, restart). Because that
  clears "started", the first frame of the new sequence is shown for one extra world update. 9
  sequences end this way.
* A frame with both stop and goto (`trails.omt` `critter_turtle2`, flags 0xa) stops: the stopped
  check comes first, so its goto is never taken.

### 5.5 ProcessFrame — `0x4497b0` (each time a frame becomes current)

1. Decode the Smacker frame (§6), unless this is a still-image anim.
2. flags & 2: stopped = 1; the result is "stop".
3. flags & 1: if the anim has a node, call node vt+0x7c(anim, current sequence, index of the frame
   in its sequence). Broadcast −1001.
4. flags & 0x10: play the frame's sound: `0x44c3c0(name, -1, 0, 0, 1, 0, -1, -1, 0, 0)` (sound
   system; the name is a WAVE chunk name).
5. flags & 0x20: get or create the anim named +0x38 (`0x454ce0`) and call its SetSequence(+0x4e, 1).
6. flags & 0x40: find the anim named +0x3c (`0x455020`); Pause(1) and Hide it.
7. flags & 0x80: start the script named +0x40 (`0x455890`); flags & 0x100: stop the script named
   +0x44 (`0x455c10`).

Only flags 1, 2, 4, 8 and 0x10 occur on the disc.

## 6. Smacker frames

### 6.1 Setup (in the v2 loader `0x4485b0`)

* `SmackOpen(fileHandle, 0x1600, -1)` reads the whole SMK into memory (PRELOADALL, no audio). The
  file pointer must end exactly after the SMK data; the loader resyncs its stream with
  `SetFilePointer(h, 0, NULL, FILE_CURRENT)`.
* Canvas: an offscreen world of the SMK's width × height. The DirectDraw surface world (`0x473be0`)
  is used if the screen device says so (device vt+0x3c), else the GDI world (`0x458390`). It is created
  8 bpp in system memory (global `0x80ea84` = 1 during creation) with colour key **RGB
  (0xFFFF, 0, 0xFFFF) = magenta**. Unless the screen is 8 bpp, the game palette (`0x80a3cc`) is
  attached first. The pixels are filled with **0xFE** and handed to
  `SmackToBuffer(smk, 0, 0, pitch, height, pixels, 0)`.
* At 16 or 32 bpp a second canvas of the same size and the screen depth (+0x130) is what gets drawn.
  At 8 bpp +0x130 = +0x128.
* All frames of the anim get the same canvas (`frame.SetCanvas(+0x130)`, flag 0x200).

### 6.2 Per frame — `0x449920`

1. Return if there is no Smacker canvas (still-image anim).
2. `SmackGoto(smk, frame.srcIndex)`. srcIndex is 1-based, which is also SmackGoto's convention.
   Sequences may show frames in any order, so the decoder needs random access.
3. Lock the canvas, `SmackDoFrame(smk)`.
4. Cropped anims (+0x124 = 1): fill every byte outside the top-left `frame.w × frame.h` box
   (SMKI) with 0xFE, then mark the anim changed.
5. Full-frame anims: if the renderer's mode is 3, add each rectangle from `SmackToBufferRect(smk, 0)`
   to both of the renderer's dirty lists. The rectangles are offset by the viewport origin
   (viewport+0xac/+0xb0) **but not by the anim's position**, so they are only right for anims at
   (0, 0). Otherwise mark the whole anim changed. Run (§5.3) marks the anim changed anyway whenever
   the current frame object changes; how the mode-3 renderer combines the two belongs to the
   renderer docs.
6. At 32 bpp: convert every pixel of the 8-bit canvas into the 32-bit canvas through the game palette
   (`*(0x80a3cc+0x9440)+8`, 256 × 0x00RRGGBB) (`0x449ba0`). At 16 bpp: RGB565 when global `0x806034`
   is set (`0x449c80`), else RGB555 (`0x449d60`).
7. Unlock.

The SMK's own palette is ignored. Index 0xFE is the colour key: palette entry 254 is magenta in the
game palette and in 713 of the 714 embedded SMK palettes.

## 7. Drawing

Each world update the node's renderer walks the layer's element list. Elements that are hidden
(+0x48 ≠ 0) are skipped. Each visible element is drawn with vt+0x78 (`0x467b20`):

* screen rectangle = GetRect (§4) + (viewport.left − trunc(viewport.camX), viewport.top −
  trunc(viewport.camY)) (viewport +0xac/+0xb0 and floats +0x40/+0x44);
* intersected with the viewport rectangle (+0xac…+0xb8) and, if enabled (+0x82), with the element's
  clip rectangle;
* then vt+0x74 (`0x4679a0`) blits the current frame's canvas to the destination world (viewport+0xa0)
  through the canvas's draw methods: whole (canvas vt+0x1c) or a sub-rectangle (vt+0x20), or one
  blit per dirty rectangle. Pixels equal to the canvas colour key (0xFE/magenta for Smacker anims;
  the BLIT/Canv conventions for still images) are skipped.

**Order**: the layer keeps its element list sorted by z, ascending. The renderer loops (`0x494db0`,
`0x4952e0`) draw the elements in list order and can stop at the first element with z ≥ 0, which only
makes sense for an ascending list. So **larger z is drawn later, on top**.
SetPosition flags the layer for resorting (layer+0x74) when z changes. Elements with equal z keep
their list order *(inferred)*. The size used everywhere is the canvas size: the whole SMK
width × height even for cropped anims, whose unused area is transparent.

## 8. Hit testing and clicks

**Point test** (vt+0x70, `0x4680b0`):

1. Fail if the anim is not visible.
2. If the clip rectangle is enabled, (x, y) must be inside it.
3. (x, y) must be inside the screen rectangle (vt+0x60), with left/top inclusive and right/bottom
   exclusive.
4. If per-pixel testing is on (+0x84, default 1), read the canvas pixel at (x − left, y − top)
   through the world's GetPixel (vt+0x68, e.g. `0x475cc0`). It is a hit only if that pixel is **not**
   the colour key.

So transparent pixels never take clicks.

**Pick** (vt+0x40, `0x4681f0` + HC `0x449740`): when the point test succeeds, the pick request gets
the anim and its canvas. If the request's +0x1e bit 0 is clear and the anim has a click message
(+0x11c ≠ 0), HC2DAnim broadcasts that message with the request through +0xf4.

**Element collision** (vt+0x44, `0x468240`): bounding rectangles first, then a test of the two
frames' 1-bit collision masks (frame+0x30, 32-pixel words, `0x468520`). HC2DAnim never builds masks,
so this path returns 0 for anims *(inferred)*. The game uses the point-sampling GetCollision of §10.4
instead.

Per-pixel testing (+0x84) is on by default. Several nodes switch it off for some anims, which then
hit-test by rectangle only.

## 9. Position, visibility, motion

### 9.1 Position and depth

The local position (+4/+8) is where the anim's frame offsets are measured from. A frame's bitmap
top-left = absolute position + frame offset, truncated toward zero. Depth = z (+0xc), set from the
sequence's z on every SetSequence (§5.1). `0x449e40` (called by some game nodes) re-centres an anim:
every frame's offset becomes (−w/2, −h/2) of the current canvas, and the position is reset to (0, 0).

### 9.2 Visibility and pause

Hide/Show (vt+0x30) is a counter. An anim is also hidden automatically when a sequence without a stop
frame runs out, and shown again by any SetSequence. Pause (vt+0x38) is a separate counter for playback
and motion. A paused anim is still drawn. Frame flag 0x40 pauses and hides another anim.

### 9.3 Motion

Mover sub-object at +0x1c (vtable `0x7f4128`):

* `0x467230` sets a per-update delta (+0x20/+0x24/+0x28).
* `0x467290` sets a speed in units per second. It is stored ×0.001 per ms at +0x2c/+0x30/+0x34 and
  multiplied by the world elapsed time.

The tick (`0x468ef0`) calls Move with the delta unless motion is paused or the anim has no layer.

## 10. Creation, registry, nodes and scripts

### 10.1 Registry and creation

All anims live in one global, case-insensitive map from name to HC2DAnim (red-black tree, root at
`0x80a410`). They are not owned by a node.

| Function | Behaviour |
|---|---|
| `0x455020` FindAnim(name) | map lookup |
| `0x454ce0` GetOrCreateAnim(name, newName, file, stream) | see below |
| `0x41d100` (node) GetAnim(name, newName) | GetOrCreateAnim(name, newName, 0, 0), then SetPosition(view origin node+0x94→+0x40/+0x44, keeping z). `0x41d0e0` is the same without the move. |
| `0x4550b0` DeleteAnim(name) | Hide, erase from the map, queue for deletion. The queue (`0x80a430`, count `0x80a434`) is flushed by `0x4552d0` from the node tick `0x456f30`, the node destructor and DeleteAllAnims. |
| `0x455180` | delete by pointer |
| `0x4551b0` DeleteAllAnims | at application shutdown (`0x44a650`) |

GetOrCreateAnim (`0x454ce0`):

* An empty name returns 0.
* If the anim exists and no newName is given:
  * if it is not linked into a layer (+0x3c = 0) and there is a current node
    (`[0x80a384]+0xec`), it calls SetNode(node), Link(node), SetPosition(0, 0, z) and
    SetSequence(0, 1);
  * an anim that is already linked is returned untouched.
* Otherwise it constructs a new anim, `0x447a00(app, name, 0, file, stream)`, which loads it (§1 of
  the format doc). If no file was given and there is a current node, it is attached the same way. A
  newName renames the copy (+0x100/+0x104). The anim is inserted into the map under its (new) name;
  an existing entry with the same name is overwritten and that anim leaks *(inferred)*. With
  logging enabled (`0x80a40c`) it logs `Anim "%s" created in %ld milliseconds.`
* **Copying** is therefore a full reload from the source resources under a new name, with its own
  Smacker handle and canvas.
* Anims created by preloading a `.omt.cache` (file ≠ 0) are **not linked**. They join the current
  node the first time someone asks for them by name.

When a node is destroyed (`0x454880`), every anim linked to it (+0x3c = node) is unlinked and
deleted; anims with a parent element are first re-linked to the node. Unlinked anims (cache
preloads, the common toolbar and pager anims) survive node changes.

### 10.2 Node callbacks

Called on the owning node (+0x114). HCNode vtable `0x7f45f8`:

| Node vt | Called from | HCNode default |
|---|---|---|
| +0x74 (anim, seq) | SetSequence | `0x458cf0`, no-op |
| +0x78 (anim, seq) | stop frame reached, or a non-looping sequence ran out | `0x454ca0`: posts script event **0x6c** {anim name pointer, seq} through `[0x80a43c]`. This is what `WaitAnim` waits for. |
| +0x7c (anim, seq, frameIndex) | frame flag 1 | `0x458d00`, no-op |

The game-node base (HCGameNode) keeps +0x78 (`0x41c250` → `0x454ca0`) and overrides +0x7c
(`0x41c270`). Individual nodes override them (`0x40b4e0`, `0x410150`, `0x414070`, `0x420270`,
`0x42bf70`, `0x42ea90`, `0x43b180`, `0x4266b0`); these belong to the node docs.

The broadcasts on +0xf4 (−1000 at sequence end, −1001 on frame flag 1, the click message) have no
listeners in this game: the only references to anim+0xf4 are inside HC2DAnim *(inferred from a scan
for `lea reg, [reg+0xf4]`)*. An implementation can omit them.

### 10.3 Script operations

Script opcodes handled by the dispatcher `0x446590` (jump table `0x446c14`). The script VM is
documented separately. Arguments are popped from the script stack; strings are name indices and
numbers are floats (truncated where an int is needed). No anim opcode pushes a result: scripts
cannot read an anim's frame, sequence, position or state. Waiting is their only feedback.

| Op | Helper | Arguments (in script order) | Behaviour |
|---|---|---|---|
| 0x2b PlayAnim | `0x4587c0` | wait, name, seq | GetOrCreateAnim(name); Show (vt+0x30(0)); if the motion pause counter (+0x5c) ≠ 0, Pause(0); SetSequence(seq, 1); if wait, WaitAnim(name, seq). On failure logs `Script error: can't create anim %s`. |
| 0x2c PlayAnimAt | `0x458930` | wait, name, seq, x, y | as 0x2b, plus SetPosition(x, y, current z) before SetSequence |
| 0x31 WaitAnim | `0x458880` | name, seq, (2 unused) | Blocks the script only if the anim exists, its current sequence is seq and it is not stopped (+0x148 = 0). The script then waits for event 0x6c with that name pointer and seq. Otherwise it logs `Script error: can't find anim %s` and continues. |
| 0x32 StopAnim | `0x458a10` | name | FindAnim; Pause(1) and Hide (vt+0x30(1)); silent if missing |
| 0x33 DeleteAnim | `0x458a50` → `0x4550b0` | name | §10.1 |
| 0x35 CopyAnim | `0x458a80` | src, new, x, y, z | GetOrCreateAnim(src, new); SetPosition(x, y, z). On failure logs `Script error: can't copy anim %s`. |
| 0x38 MoveBy | `0x458b60` | name, dx, dy, dz | Move (vt+8). If dz = −1.0 it is replaced by the current z, which doubles z (bug). |
| 0x39 MoveTo | `0x458b10` | name, x, y, z | SetPosition; z = −1.0 keeps the current z |
| 0x2d–0x30, 0x34, 0x36, 0x37 | stubs | | pop and discard their arguments |

Pause and Hide are counters, so StopAnim twice followed by PlayAnim once leaves the anim paused but
visible. PlayAnim shows it once and unpauses it once.

Frame flags 0x80 / 0x100 start or stop a script by name: `0x455890` StartScript (map `0x80a420`)
and `0x455c10` StopScript. These are the same helpers as script opcodes 0x56 and 0x58.

### 10.4 Clicks and collision as used by the game

* Mouse picking: the event manager broadcasts −602 (`0x492db0`). The node (`0x45d6b0`) asks each of
  its layers. The layer pick (`0x495c40`) walks the z-sorted elements **from the top down**,
  skipping elements with +0x62 set (not pickable), and calls Pick (§8). The first hit wins.
  * Mouse down (−600) and up (−601) reach the node's listener `0x454b50`. If node+0x9c = 0 and
    something other than the background was hit, it calls node vt+0x54 (down) or vt+0x58 (up) with
    the pick.
  * The HCNode default (`0x454c40`) posts script event 100.
  * Game nodes override vt+0x54/+0x58 (e.g. `0x4021b0`, `0x41c340`) and switch on the picked anim's
    **+0x11c click id**, which the node assigns when it creates its buttons.
* The cursor is an anim too: `0x41beb0` SetCursor(name) hides the previous cursor (node+0xec), gets
  the new one with `0x41d100`, shows it fully and sets +0x62 = 1 so it never takes clicks. Rollover
  helpers (e.g. `0x401290`) hit-test hotspots every tick and switch the cursor's sequence.
* **GetCollision** (`0x41d980`, HCGameNode; only caller grooming `0x4140f0`):
  * It returns 0 and logs `GetCollision was passed a NULL Anim!` if either anim is null.
  * It takes B's screen rectangle (vt+0x60) and point-tests A (vt+0x70, per pixel) at B's four
    corners (l,t), (l,b), (r,t), (r,b) and at B's centre ((l+r)/2, (t+b)/2), using integer division
    that rounds toward zero.
  * It then takes A's rectangle and point-tests B at A's four corners.
  * Finally it tests **A** at A's own centre, a bug: the result is 1 whenever A's centre pixel is
    opaque.
  * Any hit returns 1.
  * `0x41db70` is a "contains" variant that needs all five points of B to hit A.
  * Right and bottom are exclusive in the point test, so corner points on those edges only hit
    pixels just outside the tested anim.

## 11. Palette

Sprites are 8-bit indices drawn with the global game palette (`0x80a3cc`; owned by the palette
driver). The SMK palettes are never applied. Palette changes (fades, palette animation) therefore
recolour sprites immediately at 8 bpp. At 16/32 bpp the conversion happens only when a frame is
decoded, so a converted frame keeps its old colours until the next decode *(inferred from code)*.

## 12. Open questions

* The layer's z-sort routine was not located. Ascending order is inferred from the renderer's
  `z ≥ 0` early exit and from the data. Whether ties keep insertion order is unknown.
* How the mode-3 (dirty-rectangle) renderer uses the Smacker rectangles of §6.2 together with the
  changed flag. Belongs to the renderer docs.
* The world's update cadence (rate manager at node+0x30…: mode 2, rate 40 ms, ceiling 500 ms, from
  `0x45c900`) decides how often Run is called. Belongs to the world/node docs.
* Readers of HCAn header flag 2 (element +0x80) and of sequence flag 1 were not found.
* The HCGameNode +0x7c override `0x41c270` (it reportedly looks for frame+0x4c = 999 and reads a
  {seq, dx, dy} record) and the per-node +0x78/+0x7c overrides were not analysed here.
