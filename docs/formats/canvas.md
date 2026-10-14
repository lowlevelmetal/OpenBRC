# `Canv` chunks: OMT 1.x canvases (`OmCv` / `OmGW` / `OPa2`)

A `Canv` chunk is a serialised `OMediaCanvas` (OMT 1.x): a header, then the canvas's graphic world
(`OMediaGfxWorld`, tag `OmGW`) with an optional palette (`OPa2`) and PackBits-compressed rows. The
game uses `Canv` for scene backgrounds, sprite frames (when no `BLIT` exists, see
[blit.md](blit.md)), palettes and bitmap fonts ([font.md](font.md)). Chunks are found by name through
the `mCTy` directory ([omt-container.md](omt-container.md)); the canvas name is **not** stored in the
chunk itself.

All fields are **big-endian** except where noted. Validated by `local/re/gfx_decode.py`:
**323/323 `Canv` chunks** decode with no slack bytes (319 × 8 bpp, 3 × 32 bpp, 1 × 1 bpp).

## 1. Readers

| Address | Function |
|---|---|
| `0x45f8a0` (thunk `0x45fbf0`) | `OMediaCanvas::read`: tag, attributes, creates the world, reads it |
| `0x45fa10` (thunk `0x45fc00`) | `OMediaCanvas::write` |
| `0x45f850` | canvas vtable +0x10: (re)create the world from the attribute mask via the world factory |
| `0x471e60` (thunks `0x458620`, `0x46ee00`, `0x473bc0`, `0x476060`, `0x47b490`, `0x498250`, `0x4a0f60`, `0x4c4240`) | `OMediaGfxWorld::read` (`OmGW`) — shared by every world class through a virtual base |
| `0x472ce0` (thunk `0x473bd0`) | `OMediaGfxWorld::write` |
| `0x49c730` (thunk `0x477100`) | `OMediaPalette::read` (`OPa2` / `OmPa`) |
| `0x49bdd0` | RLE (PackBits) unpack, 8/16/32-bit units |
| `0x492ab0` | light reader used by `HCNode` (`0x454320`) that parses the same header only to take the palette |
| `0x45fa80` | database builder for type `'Canv'` (object 0x34 bytes, ctor `0x45f6a0`, vtable 0x7f4d80), registered in `0x47be10` with priority −900 |

Stream primitives: `0x45fd10` u8, `0x45fd30` i16, `0x45fd60` u16, `0x45fd90` i32, `0x45fde0` u32,
`0x45fc50` pstring (u8 length + bytes, `0x45fcd0`). The stream byte-swaps when its swap flag (+4) is
set; resource files are big-endian.

## 2. Layout

| Offset | Type | Field | Values on disc / notes |
|---|---|---|---|
| 0 | u32 | tag | `'OmCv'` (also accepted: `'OmMC'`) |
| 4 | u32 | `attr` — world capability mask (§5) | 0x1A (270), 0x22D (51), 0x22F (1), 0x32D (1) |
| 8 | u32 | world tag | `'OmGW'` |
| 12 | i16 | `width` | 8…1200 |
| 14 | i16 | `height` | 10…6525 |
| 16 | i16 | `depth` (bits per pixel) | 8 (319), 32 (3), 1 (1); 4 and 16 are also handled |
| 18 | u8 | `packMode`: 0 = 8-bit units, 1 = 16-bit, 2 = 32-bit; anything else is an error | 0 for 1/8 bpp, 2 for 32 bpp |
| 19 | u8 | `version` | always 1 |
| 20 | i16 | `rowBytes` of the packed rows | = `width` for every 8-bit canvas; 2 for the 12-px 1-bit font; `4*width` for 32 bpp |
| 22 | i16 | `hasKey` (transparency) | 1 for 316, 0 for 7 |
| … | | **if `version == 0`:** u16 r, u16 g, u16 b — key colour (read even when `hasKey` is 0) | not on disc |
| … | | **if `version > 0` and `hasKey`:** u32 key pixel value (palette index for ≤ 8 bpp, packed pixel for 16/32 bpp) | 254 (310), 253 (2: `printdialog_*`), 0 (1: `common.omt` `font`, where index 0 is white), 0x00FFFFFF (3 fonts) |
| … | i16 | `hasPalette` | 1 for every 8-bit canvas, 0 for 1/32 bpp |
| … | | palette (§3) if `hasPalette` | |
| … | u32 | `dataSize` | bytes of row data that follow |
| … | | `height` rows: u16 `packedLen`, then PackBits data (§4) | the chunk ends exactly here |

Differences from the OMT 2.5 converter (`OMediaCanvasConverter.cpp`): `hasKey` and `hasPalette` are
16-bit here, not one byte; and the palette carries an inverse table (OMT 2.5 skips it).

## 3. Palette (`0x49c730`)

| Type | Field | Notes |
|---|---|---|
| u32 | tag | `'OPa2'` (current) or `'OmPa'` (old, no inverse table). All 319 palettes on disc are `OPa2`. |
| pstring | description | empty on the disc |
| u32 | `count` | 256 on the disc |
| `count` × 3 × u16 | r, g, b | 16-bit colour guns, 0…65535. On the disc every gun is an 8-bit value × 257, so `value >> 8` is exact. |
| 4096 × u8 | inverse table (`OPa2` only) | RGB → palette index lookup, index `(r >> 12) << 8 | (g >> 12) << 4 | (b >> 12)` |

Each entry is stored with palette vtable +0x18 (set colour, index). For `OPa2` the 4096 bytes are
read straight into the palette object and marked valid; for `OmPa` the table is built at run time
*(inferred, vtable +0x5c)*. The same 4:4:4 index is used by the screen fill path (BarbieRecomp
`api_usage.md` §3.8, table at `0x80fbf0`, rebuilt by `0x4776f0`). The tables on disc look like
Mac Color Manager inverse tables: they never return index 255 (pure white maps to index 4,
(246,247,241)), and only ~45 % of entries match a plain nearest-colour search, so a port that needs
exact RGB → index results should use the stored table rather than recompute it *(inferred)*.

Conventions seen in all scene palettes: index 0 = black, 255 = white, 254 = (255,0,255) magenta =
the transparency key. In 312 of the 313 keyed 8-bit canvases the key's palette entry is magenta (the
exception is the unused `font` canvas, key 0 = white). There are only 11 distinct palettes on the
disc (§7).

## 4. Pixel rows

`dataSize` bytes follow; the reader loads them in one block and unpacks `height` rows. For each row:
read a big-endian u16 `packedLen`, then call `0x49bdd0(&src, &dst, packedLen, rowBytes, packMode)`.
The source pointer continues from wherever the unpacker stopped; `packedLen` is only a bound (on the
disc every row consumes exactly `packedLen` bytes). After each row the destination pointer is
advanced by the world's own row pitch minus `rowBytes`.

PackBits, with the unit size given by `packMode` (1, 2 or 4 bytes):

```
dstUnits = rowBytes / unit; srcUnits = packedLen / unit
while dstUnits > 0:
    n = control unit (signed; for 16/32-bit units only the low 16 bits count)
    n >= 0      : copy the next n+1 units literally
    n == -128   : no-op
    otherwise   : repeat the next unit (1 - n) times
    (any overrun of srcUnits or dstUnits fails the read: error "bad format")
```

* For 16- and 32-bit units the control unit is read as a **native little-endian** word/dword (the
  files were packed on a PC): e.g. `DC FF FF FF` = −36 → repeat 37 times. The pixel units are
  copied verbatim.
* Pixel values are big-endian on disc. After unpacking, 32-bpp rows are byte-swapped per dword
  (→ 0x00RRGGBB) and 16-bpp rows per word; 16-bpp pixels are additionally converted from 5-5-5 to
  5-6-5 when the display is 5-6-5 (`0x806034`).
* 1 bpp: MSB first, 1 = black, 0 = white (OMT: a palette of ≤ 2 entries or none means 0 = white,
  1 = black). 4 bpp: low nibble first in OMT 2.5 *(not on disc)*.

## 5. `attr`: which world class holds the pixels

`attr` is the capability mask of the world class the canvas was saved from (the writer stores
`world->vtable+0x60()`), and the reader asks the world factory (`0x465020` singleton,
`0x465150` create) for a class that has **all** those bits. Classes are registered with
(mask, priority) by `0x464ee0` and `0x47a9f0`; the list is sorted by priority, highest first
(insertion sort in `0x4652d0`), and the first class whose mask contains `attr` wins. If none
matches and `attr` has bit 0x40, the search is retried without it. Bit 0x1000 takes a separate
path (factory vtable +0x18), and an override hook (factory +0x1c) replaces the search when set.

| Mask | Priority | Builder → class | Used by `attr` on disc |
|---|---|---|---|
| 0x22F | −1 | `0x46f300` → ctor `0x46ee20` (vtable 0x7f5478), memory pixmap world; drawn by the software blitter `0x46fb60` / `0x470e80` | 0x22D: backgrounds, large prints, 2 fonts; 0x22F: the `18bb` font |
| 0x2AD | −2 | `0x46ed90` → `0x46dd70` (`OMediaWinPixmapWorld.cpp`) | — |
| 0x604E | −10 | `0x475600` → `0x473be0` `OMediaDXSurfaceWorld` (only after DirectDraw starts) | — |
| 0x1A | −65 | `0x47f990` → `0x47f800` `OMediaCompBlitterWorld` | 0x1A: every sprite frame (270) |
| 0x32D | −127 | `0x4968d0` → `0x496710` | 0x32D: the 1-bit debug font |
| 0x62D | −127 | `0x4966a0` → `0x4962f0` (`OMediaDeepMaskWorld.cpp`) | — |

So a `Canv` with `attr` 0x1A is loaded into a **compiled blitter world**: it is read with the same
`OmGW` reader and then compiled into the program format described in [blit.md](blit.md); a `BLIT`
chunk is the same object saved already compiled. The class names for 0x46ee20 and 0x496710 are
*inferred* from their neighbourhood.

## 6. Conversion while loading (`0x471e60`)

The reader does not keep the file format blindly:

1. **Depth.** The world keeps the file depth if (`attr` has 0x100 and depth = 1) or (`attr` has
   0x400 and depth = 4). Otherwise, when the global `0x805f28` is set (it is 1 in the shipped
   executable), the world is created at the **screen depth** (8 bpp in this game). So the 32-bpp
   font canvases become 8-bit worlds at load time.
2. **Palette mode** `0x805f24` (always 1 at run time; written only by `HCNode`'s constructor).
   With a palette in the file, the reader fetches the current display palette (palette driver
   vtable +8 = GetPalette, see BarbieRecomp `api_usage.md` §7.2) and compares it with the file's
   palette (palette vtable +0x30).
   * Same palette and same depth: the rows are unpacked straight into the world (the usual case;
     a world field checked at `0x4724fb` can still force the temporary-world path).
   * Otherwise the rows are unpacked into a temporary world (class `0x46ee20`) that carries the
     file's palette, which is then copied into the real world with colour conversion
     (`0x472c54`: converter vtable +0x0C) and deleted. The conversion maps each colour to the
     current display palette *(inferred: through the display palette's 4:4:4 inverse table)*.
   * The key value is converted along with the pixels: RGB key → index with `0x46c4b0`, index or
     packed value between formats with `0x46c6a0`.
   * A branch guarded by `0x80ea80` (never written, always 0) is dead.
3. A canvas without a palette (32/1 bpp) is converted against the display palette in the same way
   *(inferred)*.

Consequence for a port: **a canvas is remapped to the display palette that is current when it is
loaded**. Scene sprites share their scene's palette, so for them this is an identity copy.

## 7. Display palette and scene palettes

`HCNode`'s constructor (`0x454320`) builds the name `"<node name> background"` (or uses an explicit
background name), finds that `Canv` in the open resource files (`0x44dca0`), reads only its palette
(`0x492a00`/`0x492ab0`), stores it at node+0x98, makes it the **display palette** (screen palette
driver vtable +4 = SetPalette) and the palette for `BLIT` worlds on deeper screens (`0x450180` →
`0x80a3cc`). It then sets `0x805f24` = 1 and loads the background sprite (`0x454ad0` →
`0x44dd90(name, preferBlit=1)`).

Palettes on the disc (by the files whose canvases use them):

| # | Used by | Notes |
|---|---|---|
| 0 | 184 canvases: appleW, applesort, beachE, bellgame, bridgeS, carousel, cert1, cottageL, mission3, opendoor, rabbitE, race, rainbowS, trails, vinegame, wishingL, yard; `common.omt` `*Out` UI sprites | "outdoor" palette |
| 2 | bulletinboard; `common.omt` `*In` toolbar / certificate sprites | "indoor"; differs from #3 in 3 entries (114, 163, 169) |
| 3 | credits, grooming, stable, tackroom; `common.omt` cursor/pager/LCD `*In` sprites | "indoor" |
| 1 | barbiesoftlogonode, mattellogonode, preferences | |
| 4, 5 | `common.omt` `printdialog`, `font` | |
| 6–10 | finalpayoff, hclogonode, intromovie, leapfrog, signin | one scene each |

`common.omt` therefore carries every shared UI sprite twice (`…In_n` for indoor scenes, `…Out_n`
for outdoor scenes): `BLIT` pixels are raw display indices and are never remapped (see
[blit.md](blit.md) §6).

## 8. Drawing a canvas

* `OMediaCanvas` forwards drawing to its world: vtable +0x1C (`0x45fb00`) → world +0x14
  `draw(dest, x, y, mask)`; vtable +0x20 (`0x45fb30`) → world +0x20 `drawRect(dest, srcRect, x, y,
  mask)`.
* Memory pixmap world (`attr` 0x22D/0x22F): `0x46f1d0` / `0x46f220` call the software blitter
  `0x46fb60` / `0x470e80` with the world's key (+0x44, *inferred*), transfer mode (+0x24) and
  level (+0x26).
  It locks both worlds, clips the source rectangle to the destination (origin at dest+0x1C/+0x20),
  and copies. Pixels equal to the key are skipped *(OMT semantics; the per-depth inner loops live
  in the generated blitters at 0x4b0000+ and were not traced)*; mode/level select blended copies.
* When both worlds are DirectDraw surfaces, the copy is `BltFast` with `DDBLTFAST_SRCCOLORKEY` if
  the source has a key (BarbieRecomp `api_usage.md` §3.6/§3.8): 8-bit source colour keying.
* Compiled blitter world (`attr` 0x1A): see [blit.md](blit.md).

Six backgrounds contain key pixels (index 254); drawn with the key they show whatever was behind.
Seven canvases have no key (`hasKey` = 0): the finalpayoff, hclogonode and intromovie backgrounds,
`hclogo`, the carousel panorama (797 × 480), the tackroom panorama (1200 × 480) and the 1-bit
debug font.

## 9. Decoding to RGBA (as `gfx_decode.py` does)

* 8 bpp: colour = palette[index] with guns `>> 8`; alpha 0 where `hasKey` and index == key.
* 32 bpp: 0x00RRGGBB from the big-endian dword; alpha 0 where `hasKey` and value == key
  (0x00FFFFFF white for the three fonts).
* 1 bpp: 1 = black, 0 = white.
* Output: `local/extract/<omt>/Canv_<id>_<name>.png`.
