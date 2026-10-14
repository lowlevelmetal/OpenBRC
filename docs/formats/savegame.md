# User files: saved games, photos, preferences, perf.inf

Everything the game writes lives under the install directory (`HD Path`, app+0xDC; see
[../engine/application.md](../engine/application.md) for how it is found). None of these files exists on the
disc except a template `Resources/perf.inf`.

| path (under HD Path) | size | written by | content |
|---|---|---|---|
| `Saved Games\savegame<N>.brc` | 0x150 | SaveGame `0x401df0` | one player record per slot N = 0..9 (§1) |
| `Photos\photos.omt<N>` | variable | BarbieCamera `0x4042d0` | OMT database of up to 10 camera photos of slot N (§3) |
| `Prefs\Preferences.omt` | 8 | Preferences `0x422300` | options of the preferences screen (§4) |
| `perf.inf` | 32 | IntroMovie dtor `0x41ebb0` | intro-movie benchmark (§5) |
| `DesktopHorse\*.bmp` | — | DesktopHorse `0x411430` | copies of the chosen coat's bitmaps (see [../engine/classes.md](../engine/classes.md)) |
| `log.txt`, `log.old.txt` | text | Log::Write `0x419380` | debug log, in the current directory (rotated at start-up) |

All integers are **little-endian** (these are raw memory dumps, unlike the big-endian `.omt` resources).
`local/re/cls_save.py` parses all of them (`cls_save.py check` validates the samples, see §6).

## 1. Player record — `savegame<N>.brc`

### 1.1 File handling

| operation | function | behaviour |
|---|---|---|
| path | statics in BarbieSaveGame.cpp | `HD + "Saved Games\" + "savegame" + sprintf("%ld", slot) + ".brc"`, e.g. `Saved Games\savegame0.brc`. The directory is created by `Godiva::Start` (`0x411c70`, `CreateDirectoryA`, result ignored). |
| save | `SaveGame(record*, slot)` `0x401df0` | sets the current slot global `0x809ac0` = slot, opens the file for writing with "open always" (no truncation — the size is always 0x150 anyway), writes exactly **0x150 bytes** from `record`, closes. Every caller passes app+0x1B0. |
| load | `LoadGame(record*, slot)` `0x428080` | sets `0x809ac0` = slot; if the file exists, reads exactly 0x150 bytes into `record` (no header, version, checksum or validation); then copies rec+0x10 into the desktop-horse global `0x7ff7b4`. Returns 1 even if the file is missing (the record is left untouched). |
| exists | `SlotExists(slot)` `0x429440` | `GetFileAttributesA` test |
| new | `CreateNewSaveGame(name, slot)` `0x409f20` | builds a default record on the stack (§1.3) and calls SaveGame |
| delete | `DeleteSavegame(slot)` `0x429690` | deletes the file, then `BarbieCamera::DeletePhotoFile(slot)` `0x404e50` |

The in-memory record is the block app+0x1B0..app+0x2FF of the `Godiva` application object; game code reads and
writes it in place and saves the whole block. Fields after app+0x300 are session state and are **not** saved
(§2). The sign-in screen loads every existing slot into a scratch buffer only to read the name; the chosen slot
is then loaded into app+0x1B0 (`SignIn::MouseDown` `0x428a00`).

**When the game saves** (24 call sites): new player (`0x409f20`); quitting with the exit dialog's save flag
(`Godiva::QuitGame` `0x412db0`, also reached when the credits end); and after progress events —
AppleSort win (`0x401bd0`), bell rung (`0x40b7b0`), every cottage visit (`Carousel` ctor `0x40f860`), final
payoff seen (`0x411810`), leapfrog coin (`0x41fcd0`), `opendoor` node entry (`0x421200`), race finished
(`0x423cc0`), finale state 2→3 (`0x42ec70`), first trail ride (`TrailNode` ctor `0x431420`), horseshoe revealed
(`0x43c930`), and the mission code (`0x43efc0`, `0x43f5a0`, `0x43fb60`, `0x43fc20`, `0x43ff10`, `0x440bb0`,
`0x441b60` ×2, `0x4423d0`, `0x443710`, `0x444850`, `0x444cb0`, `0x445080`).

### 1.2 Layout

"pad" bytes are never written by the game and contain stack garbage in real files; a reader must ignore them
and a writer may store 0. Counters are only ever incremented; code treats any non-zero value as "done".
Mission names follow [../engine/nodes.md](../engine/nodes.md) (TrailNode mission table).

| rec | app | type | name | values and meaning | written by | read by (examples) |
|---|---|---|---|---|---|---|
| 0x000 | 0x1B0 | char[12] | name | player name, 8-bit characters, NUL-terminated | new player: `strncpy(…,12)` | sign-in cards, membership card, certificates, print footer |
| 0x00C | 0x1BC | u8 | name_nul | always 0 (terminator written by `0x409f20`) | new player | |
| 0x00D | 0x1BD | u8[3] | pad | garbage | — | — |
| 0x010 | 0x1C0 | u32 | horse | coat: 0 palomino, 1 white, 2 paint, 3 dapple grey, **4 = no horse chosen yet** | Stable (`0x42b4b0`…`0x42b8a0`); Race writes `rand()%4` back if it finds 4; TrailNode resets 4 to 0 | almost every node (asset choice); 4 sends IntroMovie to the yard and the TackRoom back to the stable |
| 0x014 | 0x1C4 | u32 | saddle | 0 English, 1 fancy, 2 never saddled (new player). Race, TrailNode and Yard rewrite 2 as 0 | TackRoom::MouseDown (only when a saddle is put on) | race, trail, yard (fancy tack anims when 1) |
| 0x018 | 0x1C8 | s32[4] | horse_name | per coat (index = rec+0x10): name-book index page·6+row (0..59), −1 = unnamed. Voice `"DB0%d"` with id 0..9 → 631+id, 10 → 2055, 11..35 → 632+id, 36 → 2054, 37..39 → 668..670, 40..59 → 632+id | Stable::PickNameAt `0x42b140`; Stable ctor resets all four to −1 while rec+0x10 is 4 | toolbar SayHorseName `0x41e720`, stable |
| 0x028 | 0x1D8 | u32[4] | horse_chosen | per coat: 1 once that coat has been selected | Stable Select* | Stable |
| 0x038 | 0x1E8 | struct[5] | race_history | 5 × 0x14 bytes: {+0 u32 speed level, +4 u16 flag, +6 pad[2], +8 u32 obstacle level, +0xC u32, +0x10 u32}; defaults {0,0,–,0,0,1}. Race (`0x422d00`) takes its rival speed level from entry 4 +0 (rec+0x88) and obstacle level from entry 4 +8 (rec+0x90) and promotes them when ≥3 entries match and >3 entries have +4≠0 (speed) or +0x10=0 (obstacles). **No code writes this table after a new game**, so the race always uses level 0 *(inferred from a full scan for writers)* | new player / Godiva::Start | Race ctor |
| 0x09C | 0x24C | u32 | race_wins | races won by Barbie | Race dtor `0x423cc0` | handbook awards page (ribbons) |
| 0x0A0 | 0x250 | u32 | race_losses | races won by the rival | Race dtor | awards page |
| 0x0A4 | 0x254 | u16 | first_ride_done | 0 until the first trail ride (tutorial on rabbitE); while 0, map travel and mission "Go" buttons play DB9011 and force rabbitE | TrailNode ctor (then save); yard debug keys L/l | map, clue book, certificates book, yard signs, music selector |
| 0x0A6 | 0x256 | u8[2] | pad | garbage | — | — |
| 0x0A8 | 0x258 | u32[8] | mission_done | counters M1 locket, M2 coin (leapfrog), M3 mystery rider, M4 cottage (every carousel visit), M5 applesort wins, M6 well bottle, M7 bell rung, M8 horseshoe. All eight non-zero → finale | mission code, Leapfrog, Carousel, AppleSort, BellGame, VineGame | clue book stamps and voices, saddlebag restore, CheckAllMissionsDone `0x41e7e0`, VineGame, map/trail variants |
| 0x0C8 | 0x278 | u32[4] | certificate | counters C1 nature, C2 wildlife, C3 first aid, C4 trail | mission code `0x43efc0`…`0x43ff10` | certificates book, rider rank, handbook awards |
| 0x0D8 | 0x288 | u32[3] | badge | counters R2 horseshoe, R1 bird, R3 branches (rescues offered by the pager) | `0x444850`, `0x444cb0`, `0x445080` | pager page choice `0x41df10`, awards |
| 0x0E4 | 0x294 | u32[2] | unused_e4 | zeroed, no access found | new player | — |
| 0x0EC | 0x29C | u16 | voice_variant_a | selects alternative mission voices; never written except zeroing | — | `0x443710` |
| 0x0EE | 0x29E | u16 | voice_variant_b | idem | — | saddlebag, VineGame, M5, M6 |
| 0x0F0 | 0x2A0 | u16 | voice_variant_c | idem | — | M1 locket |
| 0x0F2 | 0x2A2 | u16 | quilt_viewed | cottage quilt close-up seen | Carousel | TrailNode stop comments |
| 0x0F4 | 0x2A4 | u16 | payoff_seen | final payoff movie seen | FinalPayoff ctor (then save) | photo album award page |
| 0x0F6 | 0x2A6 | u16 | chest_opened | cottage chest opened; also makes the fancy saddle appear in the tack room | Carousel `0x410050` | Carousel, TackRoom::CreateScene |
| 0x0F8 | 0x2A8 | u16 | well_path_open | M6 bush moved: map shows the wishing well; rabbitE can branch to wishingL | M6 `0x443710` | map, TrailNode |
| 0x0FA | 0x2AA | u16 | cottage_gate_open | M4 gate opened: map shows the cottage; rabbitE can branch to cottageL | M4 `0x4423d0` | map, TrailNode |
| 0x0FC | 0x2AC | u16 | has_coin | coin (bag item 24) is in the saddlebag | AddBagItem/RemoveBagItem `0x41d1a0`/`0x41d220` | saddlebag restore |
| 0x0FE | 0x2AE | u16[3] | c1_photos | C1 rose, tulip, daisy photographed | C1 camera `0x43f0b0`; reset by certificates-book Go | saddlebag check marks, C1 award |
| 0x104 | 0x2B4 | u16[3] | c2_photos | C2 owl, bluebird, hawk photographed | C2 camera `0x43f6b0`; reset by Go | check marks, C2 award |
| 0x10A | 0x2BA | u8[2] | pad | garbage | — | — |
| 0x10C | 0x2BC | u32 | rider_rank | 0 Secret Valley Rider, 1 Moon Rider (2 certificates), 2 Star Rider (4 certificates) | BulletinBoard ctor | membership card, bulletin board |
| 0x110 | 0x2C0 | u32 | unused_110 | zeroed, no access found | | |
| 0x114 | 0x2C4 | u32 | bb_visits | bulletin-board visits (first-visit voices) | BulletinBoard::Start | |
| 0x118 | 0x2C8 | u32 | teamphoto_views | | BulletinBoard::MouseDown | |
| 0x11C | 0x2CC | u32 | cluebook_opens | | BBClueBook::Show | |
| 0x120 | 0x2D0 | u32 | certbook_opens | | BBCertificates::Show | |
| 0x124 | 0x2D4 | u32 | handbook_opens | | Handbook::Show | |
| 0x128 | 0x2D8 | u32 | unused_128 | | | |
| 0x12C | 0x2DC | u32 | horsespage_views | handbook horses page | Handbook | |
| 0x130 | 0x2E0 | u32 | unused_130 | | | |
| 0x134 | 0x2E4 | u32 | tackroom_visits | 0 → the TackTour script runs | TackRoom ctor | |
| 0x138 | 0x2E8 | u32 | unused_138 | | | |
| 0x13C | 0x2EC | u32 | ridehint_count | times the db9013 ride hint was played | TrailNode::ScriptEnded | |
| 0x140 | 0x2F0 | u32 | pager_intro_count | 0 → pager intro voice db04027 | BarbiePager::Open | |
| 0x144 | 0x2F4 | u32 | stable_visits | greeting choice | Stable ctor | |
| 0x148 | 0x2F8 | u32 | finale_state | 0 normal; 1 all missions done (set once by CheckAllMissionsDone, windmill becomes clickable); 2 windmill clicked → finalpayoff; 3 tack room after the finale (voice DB04163, saved) | `0x41e7e0`, `0x4423d0`/`0x432600`, `0x42ec70` | cottage windmill, TackRoom |
| 0x14C | 0x2FC | u32 | help_hint_count | escalating help voice (`0x41e8f0`) | toolbar | |

### 1.3 Default records

- **New player** (`CreateNewSaveGame` `0x409f20`, written to disk immediately): name = typed text (max 12 chars),
  rec+0x0C = 0, horse = 4, saddle = 2, horse_name = −1 ×4, horse_chosen = 0 ×4, race_history = 5 × {0, 0, 0, 0, 1},
  everything else from rec+0x9C to rec+0x14F = 0 (written as dwords at 0x9C/0xA0, u16 at 0xA4, 17 dwords
  0xA8..0xEB, u16 0xEC..0xFC, unaligned dword 0xFE, u16 0x102, dword 0x104, u16 0x108, dword 0x10C, 14 dwords
  0x110..0x147, dwords 0x148 and 0x14C). Untouched: 0x0D..0x0F, 0x3E..0x3F (+0x14·k), 0xA6..0xA7, 0x10A..0x10B.
- **Session start** (`Godiva::Start` `0x411c70`, before any player is chosen): the in-memory record gets name
  "Barbie", horse 4, saddle 2, the race history and zeros for most progress fields, but leaves rec+0x18..0x37
  (horse names/flags), rec+0xF4, rec+0xF6, rec+0x148 and rec+0x14C uninitialised. It is overwritten by LoadGame
  as soon as a player is selected, so it only matters for the `/node` debug start and for the quit-on-missing-CD
  path (which saves this record to slot 0, see [../engine/nodes.md](../engine/nodes.md) open questions).

## 2. Unsaved session state (app+0x300..0x34F)

Lost on quit; initialised by `Godiva::Start`. Important for understanding why some progress does not persist.

| app | type | meaning |
|---|---|---|
| 0x300 | u16 | tutorial ride active (first ride, pseudo-mission 0x10) |
| 0x304 | u32 | last race result: 1 Barbie won, 2 rival won |
| 0x308 | u32 | race course chosen from the pager chat: 1 Christie (rabbit run), 2 Teresa (beach) |
| 0x30C | u16 | horse was being ridden when the last trail/yard node was left (yard shows it mounted) |
| 0x310 | u32 | **active mission**: 0..7 M1..M8, 8..11 C1..C4, 12 R2, 13 R1, 14 R3, 15 finale, 0x10 tutorial, 0x11 none (default) |
| 0x314 | u32 | trail where the active mission continues (0 appleW … 6 wishingL, 7 any; default 7) |
| 0x318 / 0x31C | u32 | mission id / trail saved while the tutorial runs |
| 0x320 | u16 | trail certificate earned, not yet shown |
| 0x322..0x32E | u16[7] | C4 trail-certificate progress (trail groups ridden) |
| 0x330 | u32 | C4 "last trail started" (7 = none) |
| 0x334 | u16 | low-end machine (from perf.inf, §5) |
| 0x336 | u16 | debug mode (yard: type "sevanne"+Enter / "leinad"+Enter) |
| 0x338 | u16 | trail resources kept open when the yard hands over to a trail |
| 0x33A / 0x33C / 0x33E | u16 | arrival requests for the next trail node: map travel / return from a mini-game ("stop at the mission stop") / return from the race |
| 0x340 | u16 | race finished |
| 0x342 | u16 | map "rabbit2" spot: start rabbitE at frame 0x1CC |
| 0x344 / 0x346 / 0x348 | u16 | preferences: music off / mouse control / desktop horse (§4) |
| 0x34A | u16 | save on quit (exit dialog argument) |
| 0x34C | u16 | cleared by every GotoNode |

Consequence: an accepted but unfinished mission, the C4 trail progress and a pending trail certificate are not
saved; after reloading the player must accept the mission again in the clue book / certificates book.

## 3. Photos — `Photos\photos.omt<N>`

- One file per save slot N (the current slot global `0x809ac0`), named `photos.omt` + `sprintf("%ld", N)` with no
  further extension. The directory is created by `BarbieCamera::BarbieCamera` (`0x404080`).
- The file is an OMT `0MFS` database ([omt-container.md](omt-container.md)), opened read/write/create. It holds only
  **`'Canv'` chunks with ids 1..10** — at most 10 photos per player; a new photo takes the first free id
  (`0x4044a0`); "delete photo" removes chunk id i+1 for album index i (`0x4044e0`); deleting the player deletes
  the whole file (`0x404e50`).
- Each chunk body is a serialised `OMediaCanvas` ([canvas.md](canvas.md)) of a full-screen grab: 640×480, 8 bpp,
  attribute mask 0x22D, palette copied from the node's background canvas (`TakePhoto` `0x404200`). Thumbnails
  (220×165 / 220×166) and the half-size view (320×240) are scaled at load time and never stored.
- No sample photo database was available, so this layout is derived from the code only.

## 4. Preferences — `Prefs\Preferences.omt`

Despite the name this is **8 raw bytes**, a copy of four `s16` fields of the preferences node (node+0x1CE):

| offset | type | meaning | values | app mirror (set only when OK is pressed) |
|---|---|---|---|---|
| 0 | s16 | music off | 1 = off, 0 = on (default) | app+0x344 |
| 2 | s16 | mouse control | 1 = mouse (default), 0 = keyboard | app+0x346; app+0x1A8 = inverse (1 = keyboard) |
| 4 | s16 | desktop horse | 1 = install shortcut on quit (default), 0 = remove | app+0x348 |
| 6 | s16 | intro voice pending | 1 only when the file was missing; cleared by Start, so saved as 0 | — |

Read in the Preferences ctor (`0x4216d0`; missing file → defaults 0,1,1 and intro voices); written only by
`SavePreferences` (`0x422300`, creates `Prefs\`). The application fields are not loaded at start-up; they take
effect when the player presses OK on the preferences screen, which happens at every boot.

## 5. `perf.inf`

32 bytes = 8 little-endian `float32`, written with `fwrite(buf, 4, 8, f)` and read with `fread(buf, 4, 8, f)`.
Both `fopen` calls use **text mode** (`"w"` / `"r"`, strings `0x800e98` / `0x800d7c`): the MSVC CRT inserts 0x0D
before every 0x0A byte on write and removes it again on read, and a 0x1A byte ends a text-mode read. A file
written by the original can therefore be longer than 32 bytes; a reimplementation should read it the same way
(drop a 0x0D that precedes 0x0A, stop at 0x1A) and may write it in binary.

| index | offset | value |
|---|---|---|
| 0 | 0x00 | Smacker TotalDecompTime / TotalTime (fraction 0..1, logged as "Decompression percent") |
| 1 | 0x04 | TotalBlitTime / TotalTime ("Drawing percent") |
| 2 | 0x08 | TotalReadTime / TotalTime ("Reading percent") |
| 3 | 0x0C | TotalReadSpeed ("Read Speed") |
| 4 | 0x10 | SoundSkips ("Sound Skips") |
| 5–7 | 0x14–0x1F | never assigned: uninitialised stack bytes (ignore when reading, write 0) |

The IntroMovie dtor (`0x41ebb0`) reads the file if it exists, otherwise measures the intro movie with
`SmackSummary` and writes it. Either way it computes
`lowEnd = f[0]·100 > LowEndFlag_PercentDecompression (registry, default 30) || f[4] > LowEndFlag_SoundFramesToDrop
(default 0)`, stores it in app+0x334 and writes registry `LowEndFlag_IsLowEndMachine`. The intro movie can only be
skipped when `perf.inf` already existed when the node was created.

## 6. Validation

`python3 local/re/cls_save.py check` decodes and range-checks every sample in `local/re/cls_samples/`
(copied from BarbieRecomp runs of the original code):

- `savegame0.brc`: **13/13** parse (3 real records: two fresh "Ana", one "okole" after a stable and tack-room
  visit; 10 synthetic test fixtures). The real files confirm the new-player defaults, the garbage pad bytes, the
  horse/name/chosen fields and the visit counters.
- `Preferences.omt`: **3/3** (`00 00 01 00 01 00 00 00` defaults and `… 00 00 00 00` desktop horse off).
- `perf.inf`: **5/5** (including the shipped `Resources/perf.inf`: 0.0879, 0.0, 0.0016, 8175260.0, 0.0, garbage).
- `photos.omt<N>`: no samples (layout from code only).
