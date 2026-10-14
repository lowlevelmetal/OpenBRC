# Application runtime: lifecycle, node switching, configuration, scheduling

How the Human Code application object (`HCApplication`, `E:\Projects\Omt\Godiva\HCApplication.cpp`) and
its Godiva subclass drive the game: start-up, the main loop, node ("scene") switching, configuration,
the cooperative task scheduler and frame timing. Input is in [input.md](input.md), drawing and palettes
in [display.md](display.md), sprite playback and hit testing in [animation.md](animation.md).

All addresses are virtual addresses in `Barbie Riding Club.exe` (image base 0x400000). *(inferred)*
marks conclusions not proven instruction by instruction. BarbieRecomp's `docs/api_usage.md` (§1, §2,
§9) documents the Win32 side of the same code; this file concentrates on the engine logic and only
repeats API facts where an implementer needs them.

## 1. Object model

The application is one object of 0x350 bytes, allocated in `0x413090`. Its class chain is

| Class | Constructor | Vtable(s) | Notes |
|---|---|---|---|
| OMT supervisor base | `0x46d2d0` | – | message forwarding to a parent "supervisor" |
| `OMediaApplication` (OMT 1.x) | `0x47a850` | `0x7f5a20` | creates the event manager; `+0x14` quit flag; `+0x1c` event manager |
| `HCApplication` | `0x44a0e0` | `0x7f4200` (main), `0x7f41f0` (listener at +0x20), `0x7f41e8` (periodical at +0x30) | sets `[0x80a384] = this` |
| Godiva application | `0x411bd0` | `0x7f1600`, `0x7f15f0` (+0x20), `0x7f15e8` (+0x30) | game-specific methods and data |

The object therefore has three faces: the main vtable, a *listener* sub-object at `+0x20` (receives
broadcast messages) and a *periodical* sub-object at `+0x30` (an entry of the idle-task list, §6).

### 1.1 Main vtable (Godiva, `0x7f1600`)

| Slot | Function | Meaning |
|---|---|---|
| +0x00 | `0x413020` | message handler (listener entry, see §5.3) |
| +0x04 | `0x411c50` | scalar deleting destructor (→ `0x412080` → `0x44a650`) |
| +0x08 | `0x46d5d0` | set supervisor |
| +0x0c | `0x411c70` | **Run** (start-up after init, then the main loop) |
| +0x14 | `0x44aa30` | **Init** |
| +0x18 | `0x44b160` | CD check (`common.omt` must exist under CD Path; retry/cancel box) |
| +0x1c | `0x44de80` | **RequestNode(name, fadeSteps, fadeSound)** |
| +0x20 | `0x412140` | **CreateNewNode(name)** (`Godiva::CreateNewNode`) |
| +0x24 | `0x44df10` | set current node (`+0xec`) – called by `HCNode`'s constructor |
| +0x28, +0x2c | `0x49d720` | empty hooks: after deleting / before creating a node |
| +0x30/+0x34/+0x38 | `0x411c20`/`0x411c30`/`0x411c40` | "Mattel", "Barbie ® Riding Club" (CP-1252 0xAE), "1.0" |
| +0x3c | `0x44b2e0` | **Quit** |
| +0x40 | `0x49d710` | empty hook, called with the start node name on the first switch |
| +0x44 | `0x413370` | **GotoNode(name, unused)** – what game code calls |

### 1.2 Application object layout

Offsets are from the start of the object. "HC" = set by `HCApplication`, "G" = Godiva part.

| Offset | Type | Field |
|---|---|---|
| +0x00 | vtable | main vtable |
| +0x14 | u16 | quit flag; the main loop runs while it is 0 (OMT) |
| +0x1c | ptr | event manager (also global `[0x80a5a0]`) |
| +0x20 | vtable | listener sub-object |
| +0x30 | vtable | periodical sub-object; `+0x38` (u16) is its "suspended" flag |
| +0x3c | ptr | display/window object (`0x47af10`, 0x68 bytes, see display.md §1) |
| +0x40 | i32 | 1 when a DirectDraw screen exists |
| +0x44 | ptr | the `IDirectDraw*` while the app is active, 0 while inactive (used by `0x44e900` FlipToGDISurface and `0x44e910` WaitForVerticalBlank) |
| +0x48 | list | memory-sound list (sounds without a channel) |
| +0x50 | 16 × 8 bytes | 16 sound channel queues (list head, count) |
| +0xd0 | HDIGDRIVER | Miles digital driver, 0 if sound is off |
| +0xd4, +0xd8 | list, i32 | open resource databases (`.omt`) and their count |
| +0xdc, +0xe0 | char*, u16 | HD Path (with trailing `\`) |
| +0xe4, +0xe8 | char*, u16 | CD Path (with trailing `\`) |
| +0xec | ptr | current node (`HCNode`) |
| +0xf0, +0xf4 | char*, u16 | pending node name |
| +0xf8 | u16 | node switch pending |
| +0xfc | u16 | fade step count for the pending switch (0 = no fade, max 16) |
| +0x102 | u16 | fade the sound together with the palette for the pending switch |
| +0x174..+0x180 | i32 ×3, u8 | print font parameters 10, 0, 400, 0x31 (set in Run) |
| +0x184 (G) | ptr | `BarbieCamera` (photo store, `0x404080`) |
| +0x188 (G) | ptr | object `0x404f70` (vtable `0x7f01f8`; photo UI helper *(inferred)*) |
| +0x194 (G) | i32 | "mouse coordinates" debug overlay enable; set to 0 and never written again |
| +0x19c (G) | ptr | inactivity timer (§6.4) |
| +0x1a4 (G) | i32 | default inactivity interval, 20000 ms |
| +0x1b0 (G) | 0x150 bytes | current player record (the save file image, see the save-game docs) |
| +0x334 (G) | u16 | low-end machine flag (§4.3) |
| +0x336 (G) | u16 | developer mode, enabled by the yard cheat console (input.md §6.4) |
| +0x344 (G) | u16 | music off *(inferred from `0x4133e0`)* |
| +0x348 (G) | u16 | Desktop Horse enabled (preference) |
| +0x34a (G) | u16 | "save when quitting" flag, set by the exit dialog |
| +0x34c (G) | u16 | cleared by GotoNode |

## 2. Start-up

### 2.1 Order of events

1. CRT start-up, static initialisers (log rotation `log.txt` → `log.old.txt`, CPU detection), then
   `WinMain` `0x45a4d0` stores `hInstance`, `lpCmdLine` etc. and changes the working directory to the
   exe directory (api_usage §1.1).
2. `0x413090`: allocate the application (`0x411bd0`), single-instance mutex
   `"Barbie ® Riding Club"`; a second instance brings the first window forward and exits.
3. **Init** `0x44aa30` (vt+0x14), in this order:
   1. `srand(time(NULL))`, OMT random seed `0x45dc20(time(NULL))`.
   2. Registry `Use HC Palette Driver` (default 1): install `HCWinPaletteDriver` in the GDI screen
      device (display.md §7.4).
   3. Register resource-type factories (`0x47be10`).
   4. Create the display object (`0x68` bytes) and the screen port (`0x47b4b0`), then the window
      (`0x47af10`, title "OMT APPLICATION" → renamed to the game title, icon 103, black class brush).
   5. Mouse object `0x4596d0(hwnd)` → `[0x80a52c]`.
   6. Registry `Use DirectDraw` (default 1): `0x47a9f0(3)` replaces the GDI screen device by the
      DirectDraw device and creates the DirectSound object.
   7. `0x44ba00`: choose and set 640×480×8 (message boxes 1/2 if impossible).
   8. If a DirectDraw screen exists: `+0x40 = 1`, `+0x44 = IDirectDraw*`.
   9. Screen device vt+0x14(1): **present by copying dirty rectangles** instead of page flipping
      (display.md §4.1).
   10. Register the `'BLIT'` resource factory (`0x450150`).
   11. Register the application's listener with the raw-message broadcaster `[0x825420]`, the key
       broadcaster (event manager +0x18) and the mouse broadcaster (event manager +0xc), in that order.
   12. Create the script-event broadcaster `[0x80a43c]` (`0x45c2b0`); HCNode requires it.
   13. Registry `Sound` (default 1): Miles start-up (api_usage §5.1).
   14. `Use Debug Paths`, then `HD Path`/`CD Path` (a trailing `\` is appended). A missing value shows
       string 3 and quits.
   Any C++ exception escaping Init is logged, string 6 is shown and the app quits.
4. **Run** `0x411c70` (vt+0x0c):
   1. CD check `0x44b160`; Cancel calls `0x412db0(1)` (save + quit, §3.5) and Run continues into a loop
      that ends at once.
   2. Print font defaults (+0x174..), open `commonhd.omt` then `common.omt` (not as caches).
   3. Build `HD Path + "Saved Games\"` (global string `[0x809aa8]`) and `CreateDirectoryA` it.
   4. `+0x194 = 0`; `BarbieCamera(HD Path)` → `+0x184`; globals `0x7fe3c4/0x7fe3c8 = 320/240`
      *(thumbnail size, inferred)*; object `0x404f70` → `+0x188`.
   5. Inactivity timer: `0x42fb10(+0x1a4 = 20000)` → `+0x19c`, message 50000, the app listens to it.
   6. Initialise the player record (+0x1b0, 0x150 bytes) and the fields after it up to +0x34a (name "Barbie", +0x1c0 = 4, +0x1c4 = 2, the rest 0;
      +0x310 = 0x11, +0x314 = 7, +0x330 = 7).
   7. `srand(GetTickCount())`, hide the OS cursor (`0x46d7a0`), `GotoNode("MattelLogoNode")`.
   8. Main loop `0x47a970`: `while (quit flag == 0) devote_time();` (§6).
5. After the loop, `0x413090` deletes the application (§3.6).

### 2.2 First node and the start-up chain

GotoNode only *queues* the switch; the first switch happens in the first scheduler pass (§3.2). On that
first switch only, the command line is scanned for a token `/node` or `-node` (case-insensitive,
tokens split on spaces) and the following token replaces the queued name. A reimplementation should
offer the same as a developer option (`--node <name>`).

The normal chain is MattelLogoNode → HCLogoNode → BarbieSoftLogoNode → preferences → signin → then
intromovie (new player) or the player's scene; see the node documents for the conditions. The logo nodes
receive the next node's name from the factory (§3.1) and advance with `RequestNode(next, 16, 1)`
(`0x421190`), i.e. with a 16-step fade that also fades the sound.

## 3. Nodes

### 3.1 The node factory (`Godiva::CreateNewNode`, `0x412140`)

The name is compared case-insensitively (`_strcmpi`) against this list, in this order. Each match prints
a banner with `OutputDebugStringA`, allocates the object with `operator new` and calls the constructor
with `(app, name[, next, timeout])`.

| Name | Size | Constructor | Extra arguments |
|---|---|---|---|
| MattelLogoNode | 0xd0 | `0x420f60` | next "HCLogoNode", 15000 |
| HCLogoNode | 0xd8 | `0x4194f0` | next "BarbieSoftLogoNode", 15000 |
| BarbieSoftLogoNode | 0xd0 | `0x40a080` | next "preferences", 15000 |
| preferences | 0x1fc | `0x4216d0` | |
| signin | 0x25c | `0x427370` | |
| stable | 0x25c | `0x429d90` | |
| tackroom | 0x2a0 | `0x42c0b0` | |
| bulletinboard | 0x1f8 | `0x40def0` | |
| grooming | 0x258 | `0x413940` | |
| carousel | 0x224 | `0x40f860` | |
| applesort | 0x270 | `0x4015e0` | |
| leapfrog | 0x270 | `0x41f1f0` | |
| yard | 0x214 | `0x43cda0` | |
| vinegame | 0x28c | `0x43c0c0` | |
| bellgame | 0x25c | `0x40a980` | |
| opendoor | 0x1c4 | `0x421200` | |
| finalpayoff | 0x1c4 | `0x411810` | |
| intromovie | 0x1c8 | `0x41e990` | |
| race | 0x284 | `0x422d00` | |
| appleW | 0x7fc | `0x40a2f0` | |
| cottageL | 0x7fc | `0x40a450` | |
| bridgeS | 0x7fc | `0x40a500` | |
| rainbowS | 0x7fc | `0x40a660` | (a second, unreachable "rainbowS" test follows rabbitE/beachE) |
| rabbitE | 0x7fc | `0x40a5b0` | |
| beachE | 0x7fc | `0x40a3a0` | |
| wishingL | 0x7fc | `0x40a710` | |
| credits | 0x1c8 | `0x410d20` | |
| anything else | 0x214 | `0x43cda0` (yard) | after printing `"  *** ERROR ***  "` twice |

The 15000 passed to the three logo nodes is **ignored** by their constructors (the Mattel and BarbieSoft
constructors return with `ret 0x10` without reading it). Mattel and BarbieSoft logos run a 3000 ms
timer (`0x42fb10(3000)`, message 13030 = 0x32e6) started by their Start hook, and also advance on left
or right mouse button release or any key press (vt+0x60/+0x68/+0x6c = `0x419ed0`); HCLogoNode
advances when its "music" sound ends and has its own 8000 ms timer. Details belong to the logo documents.

Every node constructor runs `HCNode::HCNode(app, name, canvasName = NULL)` (`0x454320`) first, directly
or through the game-node base (`0x41b690`). Relevant for this document:

* It requires the script-event broadcaster `[0x80a43c]` and **registers itself as the current node**
  through app vt+0x24 (`app+0xec = this`) – before the factory returns.
* It loads the `'Canv'` chunk named `"<name> background"` from the open databases and installs that
  canvas's palette as the screen palette (display.md §3.2, §7.2).
* Registry `Debug Display` ≠ 0 enables the debug overlay (§8.1).
* The game-node base ("HCGameNode", `0x41b690`, used by every node except the three logos)
  additionally opens `commonin.omt.cache` and `commonout.omt.cache` once per session and creates the 34
  shared HUD anims (input.md §8.3).

`app_validate.py` confirms that all 27 factory names have a `<name>.omt` file and a `"<name> background"`
canvas reachable through `commonhd.omt`, `common.omt` or that file (27/27).

### 3.2 Requesting a switch

* **RequestNode** `0x44de80(name, steps, fadeSound)`: stores the name at +0xf0, `+0xfc = min(steps, 16)`,
  `+0x102 = fadeSound`, `+0xf8 = 1`. Nothing else happens until the application's periodical runs.
* **GotoNode** `0x413370(name, ignored)`: saves the current Miles master volume in `[0x7ffa64]` (0 if no
  driver), clears +0x34c and calls `RequestNode(name, 16, 0)` – a palette fade without a sound fade.

Several requests before the next pass simply overwrite each other; the last one wins.

### 3.3 The switch (`0x44b4e0`)

Called from the application's periodical (`0x44b4b0`) when `+0xf8 ≠ 0`. `vol` below is the Miles master
volume read at entry (0 without a driver).

**If a node is current** (`+0xec ≠ 0`):

1. `node->Pause(1)` (vt+0x2c, §6.3).
2. Fade out: `0x44df20(out=1, target = fadeSound ? 0 : vol)` (display.md §6.1). With `fadeSound = 0` the
   target equals the current volume, so only the palette fades.
3. `node->Leaving(nextName)` (vt+0x4c). Game nodes use it to stop all sounds and select the next node's
   music (`0x4133e0`, sound.md §10.1).
4. Remember `node name + ".omt"`, clear `+0xec`, then delete the node (vt+0, flag 1). With the memory
   logging flag `[0x80a388]` set, a memory report is logged before deleting.
5. `0x44d9c0(old ".omt")`: close that file in the CD and HD directories (reference counted).
6. `0x44dbb0`: delete every database in the list whose reference count dropped below 1.
7. app vt+0x28 (empty hook).

**If no node is current** (first switch): parse the command line (§2.2), then call app vt+0x40 (empty)
with the name.

**Then, in both cases:**

8. Open `<name>.omt` with `0x44d510(name, prepend=0, cache=0)`; HD copy first, then CD copy (both are
   kept open; the HD copy is searched first).
9. app vt+0x2c (empty hook), `t0 = GetTickCount()`.
10. `node = CreateNewNode(name)`; `+0xec = node`. Log `Node "%s" created in %ld milliseconds.`
11. Fade in: `0x44df20(out=0, target=vol)` – renders the new node's first frame off-screen and fades the
    palette (and, if it was faded out, the volume) up.
12. Flush input: remove all queued keyboard (0x100–0x108) and mouse (0x200–0x209) messages
    (`0x44b3c0`).
13. `node->Start()` (vt+0x48). In `HCNode` this broadcasts script event 0x69 with the node name; Godiva
    nodes start their timers here.
14. `+0xf8 = 0`.

Node constructors may open more databases, most often their `.omt.cache` file with
`0x44d510(name, 1, 1)`. A cache is read whole (12-byte ASCII length, an embedded `0MFS` database, then
anim records), its anims are created into the global anim table, and the file is closed again
(api_usage §8.3, `docs/formats/hcanim.md` §7). `app_validate.py` checks the length prefix of all 27
`.omt.cache` files (27/27).

The whole switch runs inside one call; no other task runs until it returns, except that the fade loops
pump nothing (display.md §6.1).

### 3.4 Other node hooks used by the application

| Node vtable slot | Called by | Meaning |
|---|---|---|
| +0x10 (`0x456d50`) | world idle, fade, redraw | update and draw one frame (force flag) |
| +0x2c | switch, fade, activation | Pause(on) |
| +0x48 | switch | Start |
| +0x4c | switch | Leaving(nextName) |
| +0x50 | Quit, `WM_QUIT` | quit notification (empty in all nodes found) |
| +0x5c/+0x60/+0x64/+0x68 | app message handler | left down / left up / right down / right up |
| +0x6c/+0x70 | app message handler | key down / key up |
| +0xcc (Godiva `0x41e630`) | exit dialog | quit the game: Desktop Horse files, then `0x412db0(+0x34a)` |
| +0xd0 (Godiva `0x41e150`) | Esc | show the exit dialog (input.md §6.2) |

### 3.5 Quitting

`Quit` (`0x44b2e0`): set the global modal flag `[0x80a438] = 1`, fade out to volume 0
(`0x44df20(1, 0)`; the fade reuses `+0xfc` from the last RequestNode, normally 16, so it always fades
the sound as well), call the redraw `0x44b440` (a no-op because the modal flag is set), notify the node
(vt+0x50), set the quit flag `+0x14 = 1` and show the OS cursor again. The main loop ends after the
current pass.

| Path | What happens |
|---|---|
| Exit dialog "yes" | node vt+0xcc: Desktop Horse copy and shortcut (api_usage §10), then `0x412db0(saveFlag)`: save the player record to slot `[0x809ac0]` if the flag is set, then Quit |
| Alt+F4 / `WM_CLOSE` | the raw-message listener calls Quit directly and swallows the message: **no save, no exit dialog** |
| CD check cancelled | `0x412db0(1)` |
| Init failure | message box, then Quit |

### 3.6 Shutdown (`0x412080` → `0x44a650`)

Godiva part: show the cursor, close `common.omt` and `commonhd.omt`, delete the inactivity timer, the
camera and `+0x188`. HCApplication part: unregister the listener from the three broadcasters, delete the
current node (and close its `.omt`), free the anim tables (`0x4551b0`, `0x455730`), delete the
script-event broadcaster, close all databases (`0x44dc20`), delete the display, show the cursor, stop
all sounds and shut Miles down, free the sound lists (api_usage §1.7, §5).

## 4. Configuration

### 4.1 Command line

Only `/node <name>` or `-node <name>` (first switch only, §2.2). Everything else is ignored.

### 4.2 Registry

All values live under `HKLM\Software\Mattel\Barbie ® Riding Club\1.0` (api_usage §1.4). DWORD values are
read with `0x44ccc0(name, default, &out)`, which uses the value only if it exists and is 4 bytes long.

| Value | Default | Read in | Effect |
|---|---|---|---|
| `Use HC Palette Driver` | 1 | Init | Installs `HCWinPaletteDriver` into the GDI screen device. It is destroyed when the DirectDraw device replaces the GDI device, so it only matters with `Use DirectDraw` = 0. |
| `Use DirectDraw` | 1 | Init | 1: exclusive full-screen DirectDraw, 640×480×8, DirectSound object. 0: the GDI device stays (window covers the desktop at its current mode; message box 1 if the desktop is not 8 bpp). |
| `Sound` | 1 | Init | 0: no Miles, `+0xd0 = 0`; every sound call becomes a no-op. |
| `Use Debug Paths`, `Debug HD Path`, `Debug CD Path` | 0 | Init | Developer path overrides. |
| `HD Path`, `CD Path` | – | Init | Required (string 3 + quit if missing). |
| `BackPageSysMem` | 0 | `0x44ba00` | Read into a local and never used. |
| `Debug Display` | 0 | every `HCNode` constructor | ≠0: FPS/memory overlay (§8.1). |
| `LowEndFlag_SoundFramesToDrop` | 0 | IntroMovie end `0x41ebb0` | threshold, §4.3 |
| `LowEndFlag_PercentDecompression` | 30 | IntroMovie end | threshold, §4.3 |
| `LowEndFlag_IsLowEndMachine` | (written) | IntroMovie end | result, never read back |

A reimplementation should expose `Sound` and the start node as options; the DirectDraw/palette-driver
switches select Win32 back ends and have no counterpart.

### 4.3 Performance measurement (`perf.inf`) and the low-end flag

When the intro movie node is destroyed (`0x41ebb0`, also the place that restores world update mode 2 and
blit presentation, display.md §4.1):

1. `SmackSummary` of the intro movie gives TotalTime, SoundSkips, BlitTime, ReadTime, DecompTime and
   ReadSpeed.
2. If `HD Path\perf.inf` exists, 8 floats are read from it (text-mode `fread`); otherwise the file is
   created with: DecompTime/TotalTime, BlitTime/TotalTime, ReadTime/TotalTime, ReadSpeed, SoundSkips and
   three unused floats. The measured values are only used when no file existed.
3. `lowEnd = PercentDecompression < decompFraction × 100 || SoundFramesToDrop < soundSkips`, using the
   registry thresholds (30 and 0). The result goes to `app+0x334` and to the registry.

The shipped `Resources/perf.inf` holds decompression 8.8 %, read 0.16 %, no sound skips, so a machine
whose HD Path contains that file is never low-end. `perf.min` is not referenced.

`app+0x334` is read only by the trail nodes (`0x431420` when a trail node is built, and `0x437e50`).
On a low-end machine, unless developer mode (`+0x336`) is on, the trail ride turns off its music and
butterflies, plays the small letter-boxed `_L` movie files inside a 600×300 letterbox and runs at 12
instead of 14 frames per second (movies.md §4.1, display.md §8). The flag changes nothing else.

## 5. Messages handled by the application

### 5.1 Raw window messages (`0x44c000`, listener of `[0x825420]`, message −108)

| Message | Action |
|---|---|
| `WM_ACTIVATEAPP` (main window, not quitting) | First `0x44b440` (fill the screen black, force a full node redraw, swap; skipped while `[0x80a438]` is set). **Deactivate:** `+0x44 = 0`, `[0x80a438] = 1`, pause the node if its pause count is 0, pause all 16 sound channels (`0x44c850`), show the OS cursor (loop until the display count ≥ 0). **Activate:** `+0x44 = IDirectDraw*` (if a DirectDraw screen exists), `[0x80a438] = 0`, unpause the node if it is paused, resume the channels (`0x44c880`), hide the OS cursor (loop until the count < 0). The redraw after re-activation comes from the following `WM_PAINT`. |
| `WM_CLOSE` | Quit; the message is swallowed so `DefWindowProc` does not destroy the window. |
| `WM_QUERYNEWPALETTE`, `WM_PALETTECHANGED` (from another window) | select and realise the palette driver's `HPALETTE` in the window DC (GDI path only matters). |

Separately, the OMT translator saves and lowers process/thread priority on deactivation and restores it
on activation (api_usage §9); nothing in the game raises them, so this is a no-op.

### 5.2 Translated events (`0x44be50`)

| Message | Action |
|---|---|
| −100 (`WM_QUIT` seen by the pump) | node vt+0x50 |
| −101 (`omsg_Event`), event type 7 (`WM_PAINT`) | `0x44b440` redraw |
| −102 mouse down | node vt+0x5c (left, type 1) or vt+0x64 (right, type 3) |
| −103 mouse up | node vt+0x60 (left, type 2) or vt+0x68 (right, type 4) |
| −104 key down | node vt+0x6c |
| −105 key up | node vt+0x70 |
| −108 raw message | §5.1 |
| −300 | log "me wants more memory, feed me" |

### 5.3 Godiva additions (`0x413020`)

Before passing messages to `0x44be50`, the Godiva handler restarts the inactivity timer on −102/−103
(mouse buttons only, not keys) and forwards message 50000 (inactivity timer fired) to the current node's
listener sub-object (`node+0x18`, slot 0).

## 6. Scheduling and timing

### 6.1 The idle-task list (OMediaPeriodical)

The OMT 1.x equivalent of `OMediaPeriodical` (base constructor `0x45be00`, vtable `0x7f4c28`) appends
the new object to a global doubly-linked list `[0x80a50c]` and keeps its list node at `+4`; `+8` (u16) is
a "suspended" flag. The destructor unlinks it. Each pass of the main loop calls `0x45c130`:

```
for task in list (in insertion order):
    if task.suspended == 0: task->spend_time()      // vtable slot +4
    next = task.listnode.next                          // read after the call
```

There is no abort flag (unlike OMT 2.5), no sleep and no frame limiter. Consequences an implementation
must keep:

* Tasks run in **creation order**. A task created during a pass after the current position runs in the
  same pass (this is how a freshly created node gets its first tick right after the switch).
* A task must not be destroyed by its own `spend_time`; the game defers such deletions (anims queued
  in `[0x80a430]` and ended scripts are deleted at the start of the next node tick). Deleting *other*
  tasks (the node switch deletes the node's world and its children) is fine.

Periodical classes found (constructor callers of `0x45be00`): the event manager `0x45dc40`, the
application `0x44a0e0`, OMT worlds `0x45c900` (every node), the Godiva timer `0x42fb10`, the Godiva
Smacker player `0x41a300`, `HCMovie` `0x453240`, OMT element classes `0x465d10`, the unused mouse
tracker `0x494620`, and game helpers `0x401160`, `0x405a70`, `0x406bb0`, `0x4269e0`, `0x4ad280`.

### 6.2 What runs in one pass

At the first pass the list is: event manager (created by the `OMediaApplication` constructor), the
application, the inactivity timer (created in Run). Each node adds its own tasks behind them.

| # | Task | Work |
|---|---|---|
| 1 | Event manager `0x45dd60` | If `PeekMessage` finds a message: `GetMessage`, `TranslateMessage`, `DispatchMessage` – **at most one message per pass**. Input handlers run synchronously inside (input.md). |
| 2 | Application `0x412060` | (debug mouse overlay if `+0x194`, never) → node switch if pending (§3.3) → sound service `0x44c990(1)`: per channel, advance the queue and delete finished sounds; delete unused memory sounds (sound.md §3.1). |
| 3 | Inactivity timer `0x42fc70` | fires message 50000 when its interval elapsed (§6.4). |
| 4 | Node world (game-node base `0x41cba0`) | poll the mouse, position the cursor anim, edge cursors and panorama scrolling (input.md §8); restore the music volume once a voice clip has ended (`0x41d3f0`, sound.md §6); then the `HCNode` tick `0x456f30`: delete queued anims (`0x4552d0`), delete ended scripts, broadcast script event 0x68 so every live script can resume (scripts.md "Scheduling per frame"), then **World::Idle** (§6.3), which updates and draws a frame when it is time. |
| 5… | Node-created tasks | timers, Smacker players (decode the next movie frame when due), game helpers – in creation order. |

A reimplementation's main loop is therefore: pump input → app task (switch, sound) → app timers →
for the current node: input polling and cursor, script tick, world update + render + present when due →
the node's other timers/movies. Rendering is not a separate step: it happens inside the world task.

### 6.3 The world clock and update modes

Every node is an OMT world (`OMediaWorld`, constructor `0x45c900`/`0x45c710`). Fields used for timing:

| Offset | Default | Meaning |
|---|---|---|
| +0x30 | 2 | update mode (vt+0x14 `0x45cd70` sets it) |
| +0x34 | 40 | frame interval in ms (vt+0x18 `0x45cdb0`) |
| +0x38, +0x3c | 0 | sample count and sum for mode 2 |
| +0x40 | 500 | maximum frame delta in ms (0 = no limit) |
| +0x48 | 0 | pause count |
| +0x4c | 0 | "force full redraw" for the current frame |
| +0x50 | – | clock: start tick |
| +0x54 | 0 | clock: frozen elapsed value (the frame delta during an update) |
| +0x58 | 0 | clock: accumulated time |
| +0x5c | 1 | clock stopped (u16) |
| +0x5e | 0 | clock frozen (u16) |

The clock pattern (also used by the Godiva timer at +0x18..+0x26): `elapsed = (stopped or frozen) ?
frozenValue : GetTickCount() − start + accumulated`; *stop* stores `elapsed` in `frozenValue` and sets
`stopped`; *restart* = stop, `accumulated = 0`, `start = now`, clear `stopped`. All arithmetic is 32-bit
unsigned milliseconds.

**World::Idle** (vt+0x0c, `0x45cf60`, reached through the periodical thunk `0x45cf50`):

1. If not frozen: `frozenValue = elapsed`. Set frozen.
2. If `maxDelta ≠ 0` and `frozenValue > maxDelta`: `frozenValue = maxDelta` (500 ms).
3. If **TimeToUpdate** (vt+0x1c): **UpdateFrame(force = 0)** (vt+0x10; nodes: `0x456d50`).
4. Unfreeze.

**TimeToUpdate** (`0x45cdf0`):

1. Paused (`+0x48 ≠ 0`) → no.
2. If the clock is stopped, restart it.
3. `e = frozenValue`. Mode 2: run the interval estimator `0x45cee0` first:
   `e == 0` → interval = 20, reset the counters; count == 16 → interval = sum / 16, reset;
   `e ≥ 500` and count ≠ 0 → interval = sum / count, reset; otherwise count += 1, sum += e.
4. Modes 0 and 2: if `e < interval` → no. Mode 1: always.
5. Restart the clock and answer yes.

So the frame delta seen by the update is the time since the previous update, clamped to 500 ms. Mode 2
(the default for every node) adapts the interval to the measured loop rate; in practice a frame is drawn
roughly as soon as at least a millisecond or two have passed *(inferred from the arithmetic)*. Anim
playback is time-based (every `HCAn` on the disc has the time flag, `docs/formats/hcanim.md` §2.1), so
the frame rate does not change animation speed. Mode 0 with interval 100 (10 updates/s) is used by
finalpayoff and intromovie while their movie plays (`0x411810`, `0x41e990`); their destructors and the
race destructor (`0x423cc0`) restore mode 2.

**UpdateFrame** (`0x45cff0`, wrapped by `HCNode` `0x456d50`, which returns at once while `[0x80a438]`
is set and refreshes the debug overlay first): see display.md §3 for the drawing; the timing-relevant
parts are that elements (anims) advance only inside UpdateFrame and only when the world is not paused.

**Pause** (vt+0x2c; world `0x45d2f0`, node `0x454bc0`): a counter. On 0→1 the clock is stopped with its
value set to 0 and every element is told to pause (vt+0x3c → element vt+0x38(1)); on 1→0 the clock is
again stopped at 0 (the next TimeToUpdate restarts it) and the elements are resumed; the node
broadcasts script events 0x66 (paused) and 0x67 (resumed). Time spent paused is therefore not seen by
animations.

### 6.4 Timers

**Godiva timer** (`0x42fb10`, vtable `0x7f3738`, 0x34 bytes): a periodical plus a broadcaster (+0xc).

| Offset | Field |
|---|---|
| +0x18..+0x26 | clock (as above); created **stopped** |
| +0x28 | interval (ms) |
| +0x2c | default interval |
| +0x30 | message number broadcast when it fires |

Methods: vt+0x08/+0x0c set/get interval, vt+0x10/+0x14 set/get default, vt+0x18 interval = default,
vt+0x1c/+0x20 set/get message, vt+0x24 restart (only if running). `spend_time` (`0x42fc70`): if running
and `elapsed ≥ interval`: stop, broadcast `(message, 0)` to the listeners, restart. Nodes start a timer
by restarting its clock directly (e.g. Mattel's Start, `0x421130`).

**Inactivity timer** (`app+0x19c`): interval 20000 ms by default, message 50000. The game-node base
resets it to the default interval when a node is created; individual nodes start it and may change the
interval (bulletin board: 450000 ms). While it runs, every mouse button event restarts it; when it fires
the current node receives message 50000 (§5.3).

### 6.5 Busy waits and vertical blank

The game has no sleeping wait. Code that waits spins on `GetTickCount` and does not pump messages:

| Where | Wait |
|---|---|
| node switch fade `0x44df60` | 50 steps of ≥ 12 ms, plus up to `interval + 1` ms before the fade-in (display.md §6.1) |
| DirectDraw `SetMode` `0x49ec70` | 1000 ms after a mode change (pumps the task list) |
| renderer `0x494b30` | spins until `GetFlipStatus` reports the last flip done |
| `IDirectDraw::WaitForVerticalBlank` `0x44e910` | only immediately before a Smacker palette change (`0x41a7e0`, `0x41afe0`, `0x41b5e0`, `0x453a20`, `0x431180`) and in `0x432c90` |

An implementation may sleep briefly when a pass did no work, but the fade must keep its 12 ms steps and
present each step (display.md §6.1).

## 7. Global singletons

| Address | Type | Role |
|---|---|---|
| `0x80a384` | `HCApplication*` | the application (set in `0x44a0e0`) |
| `0x80ea60` | supervisor* | top-level message target: the `OMediaApplication` from its constructor, then the display object once the window exists (`0x47af10`); the window procedure sends −101 to it and it forwards to the application |
| `0x80a5a0` | event manager* | message pump; +0xc mouse broadcaster, +0x18 key broadcaster, +0x24 focused key listener |
| `0x80a50c` | list* | idle-task (periodical) list |
| `0x825420` | broadcaster | raw window messages (−108); the application listens |
| `0x80a52c` | mouse* | mouse object (input.md §3) |
| `0x80ea40` | screen device* | current screen device (getter `0x46c320`); DirectDraw or GDI |
| `0x80a43c` | broadcaster* | script-event broadcaster (0x64 click, 0x66/0x67 pause/resume, 0x68 tick, 0x69 node entered, 0x6a sound done, 0x6c anim sequence done, 0x6d script ended; scripts.md "Messages") |
| `0x80a3cc` | palette* | the current node's background palette (set by `HCNode`, used when loading anims and movies) |
| `0x80a438` | u32 | modal/inactive flag: app inactive, message box open or quitting; stops node drawing and the redraw |
| `0x825418` | HWND | main window |
| `0x825430` | u16 | application inactive (set by the translator on `WM_ACTIVATEAPP`) |
| `0x825434` | u32 | OMT subsystem flags (bit 1: DirectDraw initialised) |
| `0x825438` | u32 | OMT initialised (WinMain) |
| `0x825444`/`0x825448`/`0x825440` | | hInstance, command line, nCmdShow |
| `0x825450` | u32 | windowed-mode flag; never set |
| `0x80a388` | u32 | log memory reports around node switches (never set *(inferred)*) |
| `0x80a40c` | u32 | log anim creation times (never set *(inferred)*) |
| `0x809ac0` | i32 | current save slot |
| `0x809aa8` | string | `HD Path + "Saved Games\"` |
| `0x809fa8`..`0x80a02c` | 34 × anim* | shared HUD anims from `commonin/commonout.omt.cache` (input.md §8.3) |
| `0x80a410` | map | global anim map, name → `HC2DAnim` (animation.md §10.1); unlinked anims survive node switches |
| `0x80a420` | map | global script map, name → `HCScript` (scripts.md) |
| `0x80a430`, `0x80a434` | list, count | anims queued for deletion (`0x4552d0`) |
| `0x80ea7c` | i32 | OS-cursor hide nesting count (`0x46d7a0`/`0x46d7c0`) |
| `0x80ea1c` | element* | text element that has keyboard focus |
| `0x7ffa64` | u16 | Miles master volume saved by GotoNode |
| `0x80a1e0` | char[101] | yard cheat-console buffer (input.md §6.4) |

## 8. Debug features

### 8.1 Debug display (`Debug Display` ≠ 0)

`HCNode` vt+0xbc (`0x455e10`), run by every node constructor when the registry value is set: finds
`debug.omt` (HD then CD; the disc has `Debug.omt` with one canvas `Lucida_Console_10`), opens it, builds a
bitmap font from that canvas and creates two text elements at (5, 5) and (100, 5) with depth 1e10 (in
front of everything). Each `UpdateFrame` then refreshes them (`0x4569f0`):

* `FPS: <10000/interval / 10>.<10000/interval mod 10>` – from the world's frame interval (+0x34);
* with `[0x80a388]` set, `malloc: …`, and always `  Sys free: … (RAM x.x Mb) (VM y.y Mb)` from
  `GlobalMemoryStatus`.

vt+0xc0 (`0x456970`) removes the elements and closes `debug.omt`.

### 8.2 Other debug hooks

* `HCNode` key-up `0x454c70`: key code 6 (Left) with Ctrl and Alt calls an empty memory-report stub.
* `0x412de0`: draws "Mouse (x,y)" with GDI in a black 640×16 bar; enabled by `app+0x194`, which is never
  set.
* The yard cheat console (input.md §6.4) enables developer mode `app+0x336`.

## 9. Validation

`local/re/app_validate.py` checks the data-dependent statements of this file, display.md and input.md
against every resource file on the disc:

| Check | Result |
|---|---|
| every factory node name has `<name>.omt` and a `"<name> background"` canvas reachable via `commonhd.omt`/`common.omt`/`<name>.omt` | 27/27 |
| `Canv` chunks start `OmCv`, capability mask, `OmGW` | 323/323 (masks 0x1a ×270, 0x22d ×51, 0x22f, 0x32d) |
| `OPa2` palettes with entry 0 black and entry 255 white | 318/319 (the unused `font` canvas is inverted) |
| `.omt.cache` files whose 12-byte ASCII length frames an `0MFS` database | 27/27 |
| cursor anims and their default depth | 13 names; 5000 except vinecursor 5001, TackGroomComb 30000, TackGroomBrush 3000 |

Helper scripts: `app_dis.py` (capstone disassembly of whole functions with string annotations),
`app_xref.py` (raw references, direct call sites, string lookups), `app_cursorz.py` (HCAn names and
depths via `mCTy`).

## 10. Open questions

* The exact interval sequence produced by the mode-2 estimator depends on the loop speed and the
  `GetTickCount` resolution; it should be validated against BarbieRecomp at runtime.
* `app+0x188` (`0x404f70`) and the globals `0x7fe3c4/0x7fe3c8` (320/240) are not identified with
  certainty.
* Whether any listener ever sets the "handled" flag to stop a broadcast early was not checked
  exhaustively.
