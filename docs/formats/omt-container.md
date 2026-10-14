# OMT container (`0MFS`) and the `mCTy` name directory

Every `Resources/*.omt` file, and the container part of every `*.omt.cache` file, is an OMT 1.x
*formatted stream* (`OMediaFormattedStream`, source path string
`C:\Projects\Omt\OMT Classes\Stream\Formatted\OMediaFormattedStream.cpp`): a set of chunks addressed
by **(type FourCC, signed 32-bit id)** with a directory at the end. Human Code added a second
directory, the `mCTy` chunk, that gives every chunk a **name**; the game looks resources up by name.

All container fields are **big-endian**. The payload byte order depends on the chunk type (OMT
streams are big-endian; `BLIT` programs, Smacker and WAVE payloads are little-endian).

Validated by `local/re/gfx_decode.py` (uses `local/re/omtls.py`) on all 83 containers on the disc
(56 `.omt`, 27 `.omt.cache`).

## 1. File layout

| Offset (from `base`) | Size | Field |
|---|---|---|
| 0 | 4 | magic `'0MFS'` (0x304D4653) |
| 4 | 4 | `dirOffset`: offset of the directory, from `base` |
| 8 | … | chunk payloads, back to back, in no particular order, with holes (see free list) |
| `dirOffset` | … | directory (§2) |

`base` is 0 for `.omt` files. A `.omt.cache` file starts with a 12-byte ASCII prefix — the decimal
length of the container (`dirOffset` + directory size), padded with spaces to 10 characters, then
`"\r\n"` — so `base` = 12 there. The bytes after the container are Smacker records described in
[hcanim.md](hcanim.md) §7, not part of the `0MFS` stream.

Open (`0x460370`): if the stream is empty, it writes a fresh header (`'0MFS'`, offset 0) and marks
the stream as new; otherwise it reads and checks the magic (error otherwise) and calls the
directory reader through the stream's vtable (+0x58 → `0x4621a0`).

## 2. Directory (`0x4621a0`)

| Field | Type | Notes |
|---|---|---|
| `typeCount` | u32 | number of type records |
| type records | … | `typeCount` × record below |
| `freeCount` | u32 | number of free-list entries |
| free entries | `freeCount` × (u32 offset, u32 size) | unused byte ranges inside the file (from `base`), left by rewrites |

Type record:

| Field | Type | Notes |
|---|---|---|
| `type` | u32 FourCC | e.g. `'Canv'` |
| `count` | u32 | number of chunks of this type |
| entries | `count` × (i32 id, u32 offset, u32 size) | offset from `base`; size in bytes |

The directory describes itself as a chunk of type **`'0HDR'`** with id **`'0HID'`** (0x30484944),
whose offset is `dirOffset` and whose size covers the whole directory including the free list
(checked on all 83 files). Readers should skip this pseudo-chunk. The writer (`0x462110`/`0x462170`)
re-registers it under that key before writing the directory.

Chunk types on the disc: `mCTy WAVE SMK2 SMAK SMKI Canv BLIT HCAn SCR DATA` (+ `0HDR`). Only
`mCTy` and `HCAn` occur in `.omt.cache` containers. Image formats: [canvas.md](canvas.md),
[blit.md](blit.md), [font.md](font.md); animations and caches: [hcanim.md](hcanim.md).

## 3. `mCTy` name directory

One chunk of type `'mCTy'` with id **−1111** (0xFFFFFBA9) per container. Reader `0x45a9d0`
(called from the directory object's constructor `0x45a580`, vtable 0x7f47e8), writer `0x45ae60`.

| Field | Type | Notes |
|---|---|---|
| `typeCount` | u32 | |
| type blocks | … | `typeCount` × block below |
| (slack) | … | some chunks have leftover bytes after the last block (e.g. `Debug.omt`: an old `"Soopafresh_12v"` entry); ignore them |

Type block:

| Field | Type | Notes |
|---|---|---|
| `type` | u32 FourCC | |
| `flag` | i16 | 0 or 1 (303 × 0, 29 × 1 on the disc). Stored at type-entry +0x14. **The game never interprets it**: it is only read, copied (`0x45b690`) and written back (`0x45ae60`). |
| `count` | u32 | number of names |
| names | `count` × (i32 id, pstring name) | pstring = u8 length + bytes (no terminator), reader `0x45fcd0`/`0x45fc50` |

The table includes an entry for itself: type `'mCTy'`, id −1111, name `"Untitled"`.

After reading, `0x45b030` reconciles the table with the container: it drops type blocks whose type
has no chunks and names whose (type, id) does not exist, and gives every chunk that has no name the
name `"Untitled"`. The writer `0x45ae60` only runs when the directory object's own +0x14 flag is 0.

### Lookup

* `0x451020` finds an id by (type, name) in one container. Names are kept in a sorted map and
  compared with `_strcmpi`, so **lookups are case-insensitive**. No container on the disc has two
  names of the same type that differ only in case.
* `0x44dca0(type, name, &id)` and `0x44dd10(type, name)` search **every open resource file** of the
  application (list at app+0xd4) in order and return the first hit; `0x44dd10` also loads the
  object (database vtable +0x0c).
* `0x44dd90(name, preferBlit)` is the still-image lookup used by sprites and backgrounds: when the
  screen is 8 bpp and `preferBlit` is set it tries type `'BLIT'` first, then `'Canv'`
  (see [blit.md](blit.md) §6).

Coverage: all 3941 names on the disc resolve to existing chunks; every `Canv` (323) and `BLIT`
(333) chunk is named.
