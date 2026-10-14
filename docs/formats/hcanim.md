# HCAn sprite animations, SMK2 / SMAK / SMKI frames and `.omt.cache` files

Sprite animations ("anims") are `HC2DAnim` objects (`E:\Projects\Omt\Godiva\HC2DAnim.cpp`). An anim
is described by an `HCAn` chunk (sequences, frames, timing, positions) and draws its pixels from one
of three frame sources, found **by the anim's name**:

| Source | Chunks | Used by |
|---|---|---|
| Smacker, full frames | `SMK2` (an embedded `.smk` file) | 275 anims in `.omt`, 83 cache records |
| Smacker, cropped frames | `SMAK` (an embedded `.smk` file) + `SMKI` (per-frame size) | 434 anims in `.omt`, 179 cache records |
| Still images | `BLIT` / `Canv` chunks named `<anim>_<n>` | 20 anims in `.omt` (7 of them unresolvable, see §8) |

Runtime behaviour (playback, drawing, hit testing) is in `docs/engine/animation.md`. The OMT
container and the `mCTy` name table are documented elsewhere. All OMT chunk data is **big-endian**;
the embedded Smacker files are little-endian as usual.

Function addresses refer to `Barbie Riding Club.exe`:

| Address | Function |
|---|---|
| `0x447cc0` | `HC2DAnim::Load(manager, file, name, extStream)` (vtable +0x98) |
| `0x448320` | version-1 loader (vtable +0x9c) |
| `0x4485b0` | version-2 loader (vtable +0xa0) |
| `0x44dca0` | find a chunk of a type by name in every open resource file (returns the file and the id) |
| `0x44dd90` | find a still image by name: `BLIT` first when the screen is 8 bpp, then `Canv` |
| `0x44d620` | open a resource file; for `.omt.cache` files it also reads the cache records (§7) |

## 1. Finding the description

`Load` (`0x447cc0`) gets the description as follows:

1. If no file was given, it searches all open resource files for a chunk named `name`, first of type
   `HCA2`, then of type `HCAn` (`0x44dde0`). If a file was given (a `.omt.cache` container), it
   looks up `name` as an `HCAn` in that file's `mCTy` table (`0x451020`). The lookup is
   case-insensitive (`_strcmpi`). If nothing is found it logs `Error: can't find animation %s.`
   and throws an OMT exception (`0x7dcfe0`). Passing neither a manager nor a file logs
   `Error: nothing to load animation from!` and throws as well.
2. It reads the whole chunk into a memory stream. For `HCAn` the length is the chunk size. For `HCA2`
   the chunk starts with two 32-bit values that the code byte-swaps twice, so they are stored
   **little-endian**: a value X (see §6) and the length of the description that follows.
3. It reads a big-endian `i32` version. Version ≤ 1 goes to the v1 loader, version 2 to the v2
   loader, and any other value is silently ignored, which leaves the anim empty.

**All 991 `HCAn` chunks on the disc are version 2.** No `HCA2` chunks exist. Version 1 and `HCA2` are
documented only for completeness.

## 2. HCAn version 2

The chunk is read sequentially; there is no padding. Strings are Pascal strings: a `u8` length
followed by that many bytes, with no terminator.

### 2.1 Header

| Offset | Type | Field | Meaning |
|---|---|---|---|
| 0 | `i32` | version | 2 |
| 4 | `u16` | defaultFrameMs | Duration of a frame without its own duration, in ms. Values on disc: 66 (463 anims), 100 (427), 166 (51), 83 (39), 200 (5), 333 (4), 72, 5000. |
| 6 | `u16` | defaultZ | Depth of a sequence without its own depth (§2.3). Larger values are drawn on top (engine doc §7; e.g. backgrounds 0–101, buttons 110–400, toolbar 3000, saddlebag 4500, cursors 5000). 88 distinct values from 0 to 30000. |
| 8 | `u32` | flags | See below. |

| Flag | Meaning (loader `0x4485b0`) | On disc |
|---|---|---|
| 0x1 | Time-based playback: frames last `ms` milliseconds. Without it the anim advances one frame per world update. Passed to the sequence player's `SetTimeMode` (`0x47a270`). | all 991 |
| 0x2 | Copied to the element at +0x80 *(meaning unknown)*. | none |
| 0x4 | No frame sources: a Pascal string follows (read and discarded) instead of the frame-source table, and no Smacker or still image is loaded. | none |
| 0x8 | Keep the depth: selecting a sequence does not set z to the sequence's depth (`0x449540`). | none |

### 2.2 Frame-source table (absent when flags & 4)

| Type | Field |
|---|---|
| `u16` | nSources (1…466 on disc) |
| nSources × (`i16` x, `i16` y) | Offset of source frame *i* (1-based) |

Each entry describes one source frame: Smacker frame *i* for SMK2/SMAK anims, or still image
`<name>_<i>` for still-image anims. (x, y) is the offset of the **top-left corner** of that frame's
bitmap from the anim's position, in pixels (range −317…634 × −242…477 on disc). Anims created by name
start at position (0, 0), so most offsets are plain screen coordinates. Cursors use small negative
offsets as the hotspot (`prefscursor`: (−2, −1)).

### 2.3 Sequences

| Type | Field |
|---|---|
| `u16` | nSequences (1…23) |
| per sequence: `u16` | nFrames (1…134) |
| `u32` | seqFlags |
| `u16` (only if seqFlags & 8) | z |
| nFrames × frame | §2.4 |

| seqFlags | Meaning | On disc |
|---|---|---|
| 0x4 | Loop: after the last frame, continue with the first (`0x47a3e0`). | 91 sequences |
| 0x8 | An explicit `u16` z follows. Otherwise z = defaultZ. | 51 (z 4554–4556) |
| 0x1 | Not read by any code found so far. | 1 (`trails.omt` `m4 gate`, seq 1) |

The loader stores each sequence's {seqFlags, z} as an 8-byte record in the anim at +0x108 (§5 of
the engine doc).

### 2.4 Frames

| Type | Present if | Field | Stored in frame object |
|---|---|---|---|
| `u16` | always | srcIndex (1-based index into the frame-source table) | +0x50 |
| `u32` | always | frameFlags | +0x34 |
| `u16` | flags & 0x8 | gotoSeq | +0x4c |
| `u16` | flags & 0x4 | ms (duration) | duration (else defaultFrameMs) |
| pstring | flags & 0x10 | sound name; a trailing `.wav` extension is cut off | +0x48 |
| pstring, `u16` | flags & 0x20 | anim name, sequence index | +0x38, +0x4e |
| pstring | flags & 0x40 | anim name | +0x3c |
| pstring | flags & 0x80 | script name | +0x40 |
| pstring | flags & 0x100 | script name | +0x44 |

The optional fields are read in the order listed. Frame flags take effect when the frame becomes
the current frame (`0x4497b0`), except 0x8, which takes effect when playback tries to leave the frame
(`0x449660`):

| frameFlags | Meaning | On disc |
|---|---|---|
| 0x1 | Frame event: the owning node is told the frame's index in its sequence, and message −1001 is broadcast. | 11 (`raceBarbie`) |
| 0x2 | Stop: playback stops on this frame and the node gets "sequence ended" (message −1000). Every non-looping multi-frame sequence on disc has it on its last frame, and only there, apart from 2 sequences in `stable.omt` `namelists`. Looping sequences never have it. | 2107 |
| 0x4 | Own duration follows. | 1 (`c2hawkanim`, 5000 ms pause in a loop) |
| 0x8 | Goto: when the frame's time is up, select sequence gotoSeq from its first frame. | 10 |
| 0x10 | Play a sound (WAVE chunk name) when the frame is shown. | 19 |
| 0x20 | Set another anim's sequence. | none |
| 0x40 | Pause and hide another anim. | none |
| 0x80 | Start the script with this name (`0x455890`). | none |
| 0x100 | Stop the script with this name (`0x455c10`). | none |
| 0x200 | Set at load time when the frame shares the anim's Smacker canvas, so the frame must not free it. | (runtime) |

Example: `preferences.omt` HCAn 0 `musicbtn` (64 bytes):

```
0000 0002 | 0042 | 006e | 0000 0001                 version, frameMs, z, flags
0003 | 004a 00db  0049 00da  004a 00db              3 frame sources
0003                                               3 sequences
0001 0000 0000 | 0001 0000 0002                    1 frame, seqFlags 0 | src 1, flags 2
0001 0000 0000 | 0002 0000 0002                    ... src 2
0001 0000 0000 | 0003 0000 0002                    ... src 3
```

That is version 2, 66 ms, z 110, time-based, 3 sources at (74,219), (73,218), (74,219), and 3
one-frame sequences showing source 1, 2 and 3, each a stop frame.

## 3. HCAn version 1 (no instances on disc)

The v1 loader (`0x448320`) first logs `Animation %s is version 1 format.  You should convert it to
version 2!`. Layout after the `i32` version: `i32` nSequences, then per sequence `i32` first,
`i32` last and `i32` (unused). For every *i* from first to last it creates a frame whose image is the
still image `<name>_<i>` (found with `0x44dd90`, or in the given file when there is no manager). The
frame rate is fixed at 15 fps (`0x479d60(15.0)`), the offset is (0, 0) and the last frame of each
sequence gets flag 0x2. There is no z, no Smacker support and no flag word.

## 4. Frame-source resolution (v2, `0x4485b0`)

After the header and the frame-source table:

1. **External stream** (only when the anim is created from a `.omt.cache` record, §7): the loader
   reads 6 bytes from the stream. `SMK2` selects full frames and `SMK1` selects cropped frames; any
   other tag throws an OMT error. The Smacker file follows at the stream position.
2. **HCA2**: the Smacker data is embedded in the HCA2 chunk itself (§6).
3. Otherwise it searches all open resource files (`0x44dca0`) for an **`SMK2`** chunk with the anim's
   name, then for an **`SMAK`** chunk. The search order of the files is the order of the resource-file
   list (see the resource-file docs). Several anims have both an `SMK2` in another node's file and an
   `SMAK` in their own; `tackroom.omt` `TackGroomComb` uses its own `SMAK` only because
   `grooming.omt` is not open in the tack room.
4. If none is found, the anim uses **still images**: for *i* = 1…nSources it looks up `<name>_<i>`
   with `0x44dd90` (`BLIT` first in 8-bpp mode, then `Canv`). If no manager was passed (never the
   case in this game) it looks up `Canv` in the given file instead. Each image's reference count is
   raised while the frames are built.

For Smacker sources the chunk is opened in the resource file and the file's Win32 handle is passed to
`SmackOpen(handle, 0x1600 = SMACKFILEHANDLE|SMACKNOSKIP|SMACKPRELOADALL, -1)`, positioned at the start
of the chunk data. The SMK header's width and height (fields +4 and +8 of the `Smack` struct) size an
8-bit offscreen canvas that is filled with **0xFE** and becomes the decode target
(`SmackToBuffer(smk, 0, 0, pitch, height, pixels, 0)`). All frames of the anim share this canvas. See
the engine doc for the per-frame decode.

### 4.1 SMK2 and SMAK chunks

Both chunk types hold an unmodified Smacker **version 2** file: all 714 start with `SMK2`, and each
chunk ends exactly where the Smacker data ends. Header + frame sizes + frame types + trees + the sum
of the frame sizes rounded down to 4 bytes equals the chunk size in every case. They differ in what
the frames contain:

* **SMK2**: every Smacker frame is the complete sprite image at full width × height. It is drawn at
  the frame-source offset.
* **SMAK**: every frame is **cropped and packed into the top-left corner** of the Smacker frame. Its
  real size comes from the same-named `SMKI` chunk. After decoding, the game fills the rest of the
  canvas with 0xFE (transparent), because Smacker's delta coding leaves pixels from earlier, larger
  frames there. Example: `leapfrog.omt` `Ducks` is 196×164 with 85 frames of 3×16 … 64×61 pixels;
  the per-frame offsets in the HCAn move the duck across the pond.

The loader marks the anim with +0x124 = 1 for SMAK/SMK1 and 0 for SMK2.

All 714 embedded sprite SMKs are 12×12 up to 640×480, have no audio tracks, header flags 0 and
frame-rate field 100 (10 fps).
The Smacker frame rate is **ignored** for sprites; timing comes from the HCAn. Their palettes are
also ignored: the indices are drawn with the game palette. In 713 of the 714 files palette entry 254
is magenta (252,0,252), the colour key that index 0xFE stands for.

### 4.2 SMKI

One record per frame source, read only for SMAK anims, `6 × nSources` bytes, big-endian:

| Offset | Type | Field |
|---|---|---|
| 0 | `u16` | width of the frame's content (frame +0x52) |
| 2 | `u16` | height of the frame's content (frame +0x54) |
| 4 | `i16` | always 8 (read and discarded; probably the bit depth) |

All 439 SMKI chunks are paired with a same-named SMAK in the same file. Their size equals
6 × the SMAK's frame count, and every (w, h) fits inside the Smacker frame. The loader reads
`nSources` records: `credits` has 11 Smacker frames and 11 SMKI records but uses only 10.

## 5. Still-image frames

With no Smacker source, frame *n* shows the BLIT/Canv image `<name>_<n>` (e.g.
`MouthFlapStable_1` … `_25`). `0x448320`/`0x4485b0` take the image object as the frame's canvas.
Transparency and pixels follow the BLIT/Canv formats (separate doc).

## 6. HCA2 (code path only)

`HCA2` is searched before `HCAn` and marks a self-contained anim: [`u32` X][`u32` length]
(little-endian, §1), then `length` bytes of version-2 description, then `i32` smkLength (read with the
same double swap), the Smacker file, and when X = 0 an `i32` followed by nSources SMKI records. X = 0
means cropped frames (like SMAK), anything else means full frames. After `SmackOpen` the stream
is moved to (position after smkLength) + smkLength. This is never exercised by the disc.

## 7. `.omt.cache` files

27 files: `applesort`, `bellgame`, `bulletinboard`, `carousel`, `commonin`, `commonout`,
`grooming`, `groomingdap|pal|pnt|wht`, `leapfrog`, `preferences`, `signin`, `stable`, `tackroom`,
`tackroomdap|paint|pal|white`, `trailDpl|Pal|Pnt|Wht`, `trlAnims`, `yard`, `yardintro`. A cache file
holds the anims of one node (or one horse colour) **together with their Smacker data**, so they can
be created in one sequential read without searching other files:

| Part | Layout |
|---|---|
| Container size | 12 ASCII bytes: decimal size padded with spaces to 10 characters, then `\r\n` (e.g. `"1272      \r\n"`) |
| Container | A complete `0MFS` OMT container of that size: an `mCTy` table and the `HCAn` chunks only |
| Records | One per anim, until end of file |

Record (`0x34` bytes of header + data):

| Offset | Size | Field |
|---|---|---|
| 0 | 12 | data size, ASCII, padded with spaces to 10, then `\r\n` |
| 12 | 34 | anim name, padded with spaces to 32, then `\r\n` |
| 46 | 6 | `SMK2\r\n` (full frames) or `SMK1\r\n` (cropped frames) |
| 52 | size | a Smacker file. For `SMK1`, the SMKI records (6 bytes per frame source) follow it within `size`. |

How the game uses a cache file:

1. A node constructor opens it with `0x44d510(name, 0, isCache=1)`. The HD path is tried first, then
   the CD path. The file is opened for sequential reading (`FILE_FLAG_SEQUENTIAL_SCAN`).
2. The file-object constructor (`0x44cf10`) reads the 12-byte prefix, reads the container into a
   memory stream and parses it as an OMT file.
3. The cache branch of `0x44d620` then loops while the file position is below the file size:
   * It reads the record size (`atoi`) and the name, which is cut to 31 characters with trailing
     spaces removed.
   * If an anim with that name is already registered (`0x455020`, global map at `0x80a410`), it seeks
     to `record + 0x34 + size`.
   * Otherwise it calls `0x454ce0(name, 0, cacheOMT, fileStream)`. That loads the description from
     the cache's own container and the frames from the stream (§4 step 1), and leaves the stream at
     the next record.
   * The loop also stops when creating an anim fails.
4. The file object is destroyed straight away. The cache is **not** added to the list of open
   resource files.

So opening a cache **creates all of its anims at once** (preloading), and anims that already exist
are kept. Anims are global and are looked up by name before any `.omt` is searched (`0x454ce0`), so
a cached anim wins over the same-named chunks in the `.omt` files. Some constructors skip a cache
entirely when a sentinel anim is already registered (bulletinboard: `certificatebtn`; tackroom:
`BridleEat`; tackroom horse caches: `TackAmb<horse>`). Which node opens which cache is documented
with the nodes. In short: the node base constructor opens `commonin` and `commonout` once, each
game node opens its own cache, and the trail/yard nodes open `trlAnims` and `trail<horse>`. Nothing in
the executable writes cache files; they were built offline *(inferred)*, probably to make node entry
from CD a single sequential read *(inferred)*.

Verified: all 27 files parse to their exact end. There are 262 records, one for each `HCAn` in the
same cache's container, with the same names (in 3 files in a different order).

* 260 of the 262 cache `HCAn` chunks are byte-identical to the `.omt` copies. The two exceptions are
  `bellgame` `GrassOverlay` (y 277, against 278 in the `.omt`) and `BellgameObject8` ((84,365)
  against (85,366)).
* Each `SMK2` record body is byte-identical to the same-named `SMK2` chunk (one comes from a `SMAK`).
* Each `SMK1` body is the same-named `SMAK` chunk followed by the same-named `SMKI` chunk, except
  `tackroom.omt.cache` `posTR`. Its `.omt` version is a still-image anim, so the cache is the only
  source of its Smacker frames.

## 8. Validation (`local/re/anim_dump.py`)

* `HCAn`: **991/991** parse with the v2 layout and consume the chunk exactly (729 in `.omt`, 262 in
  `.omt.cache`). Flags seen: header 0x1; sequence 0x1, 0x4, 0x8; frame 0x1, 0x2, 0x4, 0x8, 0x10.
* Frame sources resolve for 984/991: SMK2 275, SMAK 434, cache SMK2 83, cache SMK1 179, still
  images 13. Unresolvable (7): `barbiesoftlogonode background`, `grooming background`,
  `leapfrog background`, `opendoor background`, `yard background` and `Stable` (their images are
  named without `_1`, so they are probably loaded as plain backgrounds and never as anims), and
  `tackroom.omt` `pad2` (image named `pad2g1`). srcIndex is always within 1…nSources.
  The script resolves sources the way the game would with only the anim's own node files open
  (own file, then files of the same stem, then `common*`). With that, nSources = Smacker frame count
  everywhere except `credits` (10 of 11). Names that exist in several files depend on which files
  are open:
  * `gameBarbie`: HCAn and SMAK in `gameBarb.omt` with 150 frames, HCAn and SMK2 in `vinegame.omt`
    with 151. Because SMK2 is searched first in every open file, `gameBarb.omt`'s description would
    pair with `vinegame.omt`'s SMK2 if both files were open.
  * `TackGroomComb`: SMAK in `tackroom.omt` with 2 frames, SMK2 in `grooming.omt` with 1 frame.
* `SMK2` 275/275 and `SMAK` 439/439 have a valid `SMK2` header and exact size. `SMKI` 439/439 are
  paired. Cache records 262/262 are valid.
* All 976 Smacker streams (275 + 439 + 262) decode with the BarbieRecomp decoder via the harness
  `local/re/anim_smkdec.c` (`anim_dump.py --decode`). Spot checks look correct: `musicbtn` renders the
  "Music: yes/no" panel, and `Ducks` with SMKI cropping shows a single duck in the top-left box.

## 9. Open questions

* Header flag 0x2 (element +0x80) and sequence flag 0x1 have no reader identified yet.
* The exact order of the open-resource-file list decides SMK2/SMAK and name collisions between files
  (see the resource-file / node docs).
