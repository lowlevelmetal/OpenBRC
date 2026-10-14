# Input and the mouse cursor

How keyboard and mouse input travels from Win32 messages to nodes and anims, and how the software
cursor works. Win32 API details (exact message constants, `GetAsyncKeyState`, `ShowCursor`) are in
BarbieRecomp `docs/api_usage.md` §2; the application object and the scheduler are in
[application.md](application.md); element hit testing is in [animation.md](animation.md).

Addresses are virtual addresses in `Barbie Riding Club.exe`. *(inferred)* marks conclusions not proven
instruction by instruction.

## 1. Overview

```
Windows message queue
  └─ event manager task (0x45dd60): at most one message per scheduler pass
       └─ window procedure 0x45e1b0
            ├─ translator 0x45ddc0 → OMT event (§2)
            │    ├─ mouse button → mouse broadcaster  (event manager +0xc)  message −102 / −103
            │    │     listeners in registration order:
            │    │       1. application (0x413020): restart inactivity timer, node vt+0x5c..0x68
            │    │       2. current node's click tracker (0x492db0): pick → node vt+0x54 / vt+0x58
            │    │       3. listeners added by individual nodes
            │    └─ key → focused text element, else key broadcaster (event manager +0x18) −104 / −105
            │          listeners: application (node vt+0x6c / vt+0x70), listeners added by nodes
            ├─ translated event also sent as −101 to the top-level object (only WM_PAINT is acted on)
            └─ untranslated message → raw-message broadcaster (−108) → application, then DefWindowProc
Mouse position and left button state are **polled** (GetCursorPos / GetAsyncKeyState), never taken
from messages; WM_MOUSEMOVE is ignored.
```

The event manager (`0x45dc40`, 0x28 bytes, vtable `0x7f4d18`, global `[0x80a5a0]`) is an idle task plus
two broadcasters: `+0x0c` mouse, `+0x18` keyboard; `+0x24` is the focused key listener (or 0).

## 2. The OMT event

The window procedure builds a 0x18-byte event on the stack and lets the translator fill it:

| Offset | Type | Field |
|---|---|---|
| +0x00 | i32 | type: 1 left down, 2 left up, 3 right down, 4 right up, 5 key down / character, 6 key up, 7 paint, 9 swallow |
| +0x04 | u16 | modifiers: bit 0 Shift, bits 1+3 (0xA) Alt, bit 2 Ctrl – from `GetAsyncKeyState` at translation time |
| +0x08 | i32 | key code (below), 0 for characters |
| +0x0c | u8 | character from `WM_CHAR`, 0 otherwise |
| +0x10, +0x14 | i32 | mouse x, y in client coordinates, from the mouse object (not from `lParam`) |

Every translated event gets position and modifiers through `0x45e070`.

Key codes (`0x45e0d0`): Backspace 2, Tab 10, Enter 3, Escape 1, Left 6, Up 8, Right 7, Down 9, Delete 4.
Every other virtual key gives 0. There are no function-key, Page or Home codes.

### 2.1 Translator (`0x45ddc0`)

| Message | Event | Dispatch |
|---|---|---|
| `WM_LBUTTONDOWN` | type 1 | Clears the focused key listener, broadcasts −102 to the mouse listeners; if afterwards a different listener holds the focus, the old one receives −109 (lost focus). |
| `WM_RBUTTONDOWN` | type 3 | −102 to the mouse listeners |
| `WM_LBUTTONUP` / `WM_RBUTTONUP` | type 2 / 4 | −103 to the mouse listeners |
| `WM_KEYDOWN` | type 5, key = mapped code, char 0 | −104 to the focused listener, else to the key listeners |
| `WM_CHAR` (except Backspace) | type 5, key 0, char = `(BYTE)wParam` | same as key down |
| `WM_KEYUP` | type 6, key = mapped code | −105, same routing |
| `WM_PAINT` | type 7 (type 9 when DirectDraw is active and the app is inactive) | `BeginPaint`/`EndPaint` only; the application redraws (application.md §5.2) |
| `WM_ACTIVATEAPP` (main window) | none | sets the inactive flag `[0x825430]`, priority save/restore; the application's raw listener does the rest (application.md §5.1) |
| everything else | none | raw broadcast, then `DefWindowProcA` unless a listener zeroed the message |

Consequences:

* A typed character arrives **twice**: once from `WM_KEYDOWN` (key code, usually 0, no character) and
  once from `WM_CHAR` (character, key 0). Enter gives key 3 *and* character `'\r'`; Escape gives key 1
  and character 27; Tab key 10 and character 9. Backspace only comes from `WM_KEYDOWN` (key 2).
* `WM_SYSKEYDOWN`/`WM_SYSCHAR` are not translated, so Alt combinations reach `DefWindowProcA`; Alt+F4
  produces `WM_CLOSE`, which the application turns into an immediate Quit without saving
  (application.md §3.5).
* For translated messages the window procedure returns 0 without calling `DefWindowProcA`.

## 3. Mouse polling

Mouse object (`0x4596d0`, 0x10 bytes, vtable `0x7f47a8`, base vtable `0x7f5fb0`, global `[0x80a52c]`):

| Offset | Field |
|---|---|
| +0x04 | optional tracker/recorder (`0x494620`), never created by the game |
| +0x08, +0x0a | i16 offset added to every position (0) |
| +0x0c | window handle |

| Slot | Function | Behaviour |
|---|---|---|
| vt+0x04 | `0x494420` | left button down: `GetAsyncKeyState(VK_LBUTTON) >> 15` |
| vt+0x08 | `0x4944e0` | **GetPos(&x, &y)**: tracker position if a tracker exists, else vt+0x1c; plus the offset |
| vt+0x0c | `0x494540` | SetPos: through the tracker, else vt+0x20 |
| vt+0x10/+0x14 | `0x494590`/`0x494600` | start/stop the tracker (unused) |
| vt+0x1c | `0x459720` | `GetCursorPos` + `ScreenToClient` |
| vt+0x20 | `0x494460` | `SetCursorPos(x, y)` unless the app is inactive |

Every game use of the mouse object is GetPos (vt+0x08). Coordinates are 640×480 client coordinates;
the window is a borderless popup at (0, 0), so client and screen coordinates are equal.

### 3.1 Re-centring in the riding games

The race and trail-ride nodes read relative motion by warping the pointer with a direct
`SetCursorPos` (`0x422ce0`, not the mouse object, so it also runs while inactive):

| Site | Call | Remembered value |
|---|---|---|
| `0x424834` (race start, `0x424390`) | (320, 240) | `[0x80a078] = 240` |
| `0x4251b7` (race) | (x, 240) | `[0x80a078] = 240` |
| `0x434f2d` (trail start, `0x434cb0`) | (320, 240) | – |
| `0x436778` (trail ride, `0x435980`) | (x, 240) | `[0x80a1b0] = 240` |
| `0x43691c` (trail ride) | (320, y) | `[0x80a1c8] = [0x80a1bc] = 320` |

The next GetPos must return exactly the warped position. A reimplementation should keep a virtual
pointer in game coordinates, apply relative host motion to it, and let `SetCursorPos` overwrite it
(host relative-mouse mode while these nodes run). The meaning of the motion belongs to the race/trail
documents.

## 4. Mouse buttons to nodes

### 4.1 Application path

The application is the first mouse listener (registered in Init). On −102/−103 it restarts the
inactivity timer if it is running (application.md §6.4) and calls the current node's virtual handler
with the event: left down vt+0x5c, left up vt+0x60, right down vt+0x64, right up vt+0x68. In `HCNode`
these four are empty; the logo nodes use them to skip ahead.

### 4.2 Click tracker and picking

Every `HCNode` constructor creates a click tracker (`0x492cb0`, 0x2c bytes, vtable `0x7f5da0`) at
`node+0xa0`, registers it as a mouse listener and adds the node's listener sub-object (`node+0x18`) to
both of the tracker's broadcasters (+0x10 pick, +0x1c click). On a mouse button event (`0x492db0`):

1. Build a pick request:

   | Offset | Field |
   |---|---|
   | +0x00 | result: 0 nothing, 1 background only, 2 element with an image, 4 element without an image |
   | +0x04 | view that was hit |
   | +0x08 | element that was hit |
   | +0x10 | the element's current image (world) |
   | +0x14, +0x16 | x, y relative to the tracker origin (+0x28, +0x2a; 0 for nodes); converted to view coordinates during the pick |
   | +0x18, +0x1a | copy of x, y |
   | +0x1c | modifiers |
   | +0x1e | 1 unless the event is a left button press |
   | +0x20 | event type |

2. Broadcast −602 (pick) to the node. The world's listener (`0x45d6b0`) asks each of its views in turn
   (renderer vt+0x18, `0x495c40`) until one sets a result, then marks the broadcast handled. A view
   only answers if the point lies inside its rectangle. It collects its elements in drawing order and
   tests them **from the last drawn (frontmost) to the first**, skipping elements whose "not pickable"
   flag (+0x62) is set; the element test is the hit test described in animation.md (bounding box, then
   optionally the pixel). If no element is hit and the world has a background, the result is 1.
3. If something was hit, or the event is a button release, broadcast −600 (press) or −601 (release).
   The node listener (`0x454b50`) ignores both while `node+0x9c` (input disabled) is set and for
   result 1; otherwise it calls **vt+0x54(pick)** for presses and **vt+0x58(pick)** for releases.

`HCNode` vt+0x54 (`0x454c40`) turns a press on an element with an image (result 2) into script event
0x64 (click) on `[0x80a43c]`; vt+0x58 is empty. The game-node base routes both to `0x41c340`, which
drives the exit dialog and the toolbar (`0x41c530`/`0x41c810`), so toolbar handlers run on press *and*
release.

### 4.3 Listeners added by nodes

Many node constructors also register their listener directly with the mouse broadcaster (applesort,
bellgame, bulletinboard, carousel, finalpayoff, grooming, intromovie, leapfrog, opendoor, race, stable,
tackroom, trail, vinegame, yard) and some with the key broadcaster (applesort, race, trail, yard). The
game-node base listener (`0x41c910`) uses a left-button release at x < 15 or x > 604 to send the node a
navigation message (13000, 0x32c9, 0x32ca or 0x32cc, depending on node flags), which is how screen-edge
navigation works. Hover effects are done by nodes polling GetPos each frame (§8), not by messages.

## 5. Mouse wheel, double clicks, middle button

Not handled: `WM_MOUSEWHEEL`, `WM_*BUTTONDBLCLK` and the middle button never reach the game (the window
class has no `CS_DBLCLKS`).

## 6. Keyboard

### 6.1 Dispatch

Key events go to the focused key listener `[0x80a5a0]+0x24` if there is one, else to the key
broadcaster. Focus is taken only by OMT text elements (`0x4667b0`; global `[0x80ea1c]` is the focused
element) and released by `0x466820` or by a left click (§2.1). Without focus the application forwards
key down/up to the current node's vt+0x6c/vt+0x70.

### 6.2 Keys with a meaning

| Key | Where | Effect |
|---|---|---|
| Escape (key 1) | game-node base key-down `0x41c310` | node vt+0xd0(1): open the exit dialog with "save on quit" (`0x41e150`; ignored if it is already open). Sign-in uses vt+0xd0(0) after closing its own dialogs (`0x429aa0`). |
| Enter (character `'\r'`) | sign-in `0x429aa0` | confirm (when its flag allows) |
| any key | Mattel/BarbieSoft logo key-down | skip to the next node |
| Ctrl+Alt+Left (key-up) | `HCNode` `0x454c70` | empty debug hook |
| Alt+F4 | `DefWindowProc` → `WM_CLOSE` | immediate quit, no save |

Other keys are used by specific nodes (race, trail, applesort, yard, text entry); see their documents.

### 6.3 Text entry

Typed text arrives as type-5 events with a character and key 0 (`WM_CHAR`), either at a focused text
element or at the node's key-down handler. Backspace and Delete come as key codes 2 and 4.

### 6.4 Yard cheat console (developer)

The yard node's key-down handler (`0x43d8a0`) appends every non-zero character other than `'\r'` to a
101-byte buffer `[0x80a1e0]` (index wraps to 0 after 100) and echoes it with `OutputDebugStringA`.

* Enter (key 3) compares the buffer case-insensitively and clears it: `"sevanne"` plays the sound
  `trash` and sets developer mode (`app+0x336 = 1`); `"leinad"` plays `camera` and does more (not
  traced).
* With developer mode on, single characters toggle global flags and log `YARD :: …`: `A`/`a` ambient
  on/off, `B`/`b` backups, `D`/`d` dialog, `F`/`f` butterflies, `H`/`h` horse heads, `L`/`l` tutorial
  on/off (player record +0x300/+0x254), `M`/`m` music, `O`/`o` new/old bridge, `P`/`p` programmatic letter
  box on/off, `V`/`v` video letter box on/off, `R`/`r` go to node `rabbitE`; `?` sets `[0x80a164]`
  regardless of the mode.
* Escape opens the exit dialog unless `[0x804030]` is set.

## 7. Input flush and pauses

* After every node switch the application removes all pending keyboard (0x100–0x108) and mouse
  (0x200–0x209) messages (`0x44b3c0`). Input typed during a fade or node construction is lost.
* While the app is inactive (`[0x80a438]`), nodes are paused and do not draw; input messages are still
  translated and dispatched, so handlers must cope *(inferred: no check found in the dispatch path)*.
* `MessageBoxA` (`0x44b320`) sets `[0x80a438]` and shows the OS cursor around the box.

## 8. Cursor

### 8.1 The OS cursor

The system cursor is hidden for the whole session (`0x46d7a0` in Run calls `ShowCursor(FALSE)` when its
nesting count `[0x80ea7c]` is 0 and increments it; `0x46d7c0` is the inverse). On deactivation the app loops
`ShowCursor(TRUE)` until the display count is ≥ 0, on activation `ShowCursor(FALSE)` until it is < 0;
around message boxes it shows it once. The window class cursor is `IDC_ARROW`, so the arrow appears only
while the game is inactive or a message box is up. A reimplementation simply hides the host cursor
while the game window has focus.

### 8.2 The software cursor is an anim

The visible cursor is an ordinary anim (sprite) element in the current node's world, drawn by the world
renderer in depth order like every other anim (display.md §3). Its depth is high, so it is normally on
top: every cursor anim on the disc has default depth 5000 (vinecursor 5001; `TackGroomCombCursor` 30000,
`TackGroomBrushCursor` 3000; checked by `app_validate.py`, 13 cursor anims). Anims with depth 6000 or
9999 (15 on the disc) and the debug text (1e10) draw over the cursor. The **hotspot** is the anim
frame's source offset: cursors use small negative offsets (e.g. `prefscursor` (−2, −1)), so the anim
position is the pointer position (docs/formats/hcanim.md §2.2).

**Selecting a cursor** – game-node base method `0x41beb0(name)`: hide the previous cursor anim (`node+0xec`,
vt+0x30(1)); create or fetch the anim by name (`0x41d100` → GetOrCreateAnim `0x454ce0`, animation.md §10.1, placed at
the view camera position); show it (call vt+0x30(0) until its hidden count is 0); mark it **not pickable** (+0x62 = 1)
so clicks go to what is under it; clear the edge flags `node+0xc4`, `+0xc6`, `+0xca`, `+0xcc`. Node
constructors call it with their cursor name (`signincursor`, `prefscursor`, `frogcursor`, `vinecursor`,
`TackGroomCombCursor`, …); other nodes use the shared `cursorIn`/`cursorOut` anims (§8.3) *(inferred)*.

**Positioning** – every frame, in the game-node base task (`0x41cba0`, before the world update):

1. `(x, y) = GetPos()`. If `node+0xfa == 0` (no DirectDraw screen) subtract the view's top-left; with
   DirectDraw the view is at (0, 0) anyway.
2. Panorama scrolling (if the node set a scroll speed in `node+0x158/+0x15c`, nothing blocks it, and the
   camera is not busy): move the view camera by `(now − last) × 0.01 × speed` pixels, clamped to
   `[node+0x164, backgroundWidth − viewWidth]`.
3. If the cursor anim is visible and its current sequence (anim +0xd8) is not 7
   or 8 (sequences are selected with SetSequence(seq, 1), animation.md §5.1):
   * pointer in the middle (15 ≤ x ≤ 604) and sequence ≠ 0 → select sequence 0 (normal pointer);
   * otherwise choose an edge sequence from the node's flags: `+0xc4`: 1 at x < 15, 2 at x > 604;
     else `+0xca`: 3 at x < 15; else `+0xcc`: 4 at x > 604; else `+0xc6`: 3 at x < 15, 4 at x > 604;
     else `+0xc8`: 6 at x > 604 (if not already 6).
4. Keep the image on screen: if no edge flag is set, x = min(x, 639); otherwise, if x + imageWidth > 639,
   x = 639 − imageWidth. If y + imageHeight > 479, y = 479 − imageHeight / 2 (integer division).
5. Add the view camera position (`view+0x40`, `+0x44`) and move the anim there, keeping its z.

The screen edges (x < 15, x > 604) are thus both cursor zones and click zones (§4.3).

### 8.3 Shared HUD anims

The first game-node-base node constructed opens `commonin.omt.cache` and `commonout.omt.cache` (17 anims each)
and stores these anims in globals, in this order (`0x41b690`):

| Globals | Anims |
|---|---|
| `0x809fa8`..`0x809fe8` | christieLCDIn, pagerIn, pagerNoIn, pagerRaceIn, pagerReplayIn, pagerYesIn, tbQuitIn, teresaLCDIn, **cursorIn** (`0x809fc8`), saddlebagIn, sbFlapIn, tbBagIn, tbCameraIn, tbHelpIn, tbHomeIn, tbMapIn, tbPagerIn |
| `0x809fec`..`0x80a02c` | christieLCDOut, pagerNoOut, pagerOut, pagerRaceOut, pagerReplayOut, pagerYesOut, tbQuitOut, teresaLCDOut, **cursorOut** (`0x80a00c`), saddlebagOut, sbFlapOut, tbBagOut, tbCameraOut, tbHelpOut, tbHomeOut, tbMapOut, tbPagerOut |

Each is unlinked from the node right after creation (vt+0x1c), so these anims survive node switches and
join the current node again when they are next requested by name (animation.md §10.1). "In"/"Out"
variants exist for every HUD item, so the suffix denotes two versions of the HUD (probably the indoor
and outdoor look) rather than a hover state *(inferred)*.

## 9. Checklist for a reimplementation

1. Poll the pointer every frame in 640×480 game coordinates; never derive it from motion events alone.
2. Deliver button presses/releases as discrete events, then run the pick (frontmost pickable element)
   and call the node's press/release handlers; restart the inactivity timer on every button event.
3. Deliver key presses as (key code) events and typed characters as separate (character) events.
4. Implement `SetCursorPos` as an immediate warp of the virtual pointer.
5. Drop queued input after each node switch.
6. Draw the cursor as an anim in the world (depth 5000), not as an overlay.
