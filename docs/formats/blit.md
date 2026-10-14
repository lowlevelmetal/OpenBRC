# `BLIT` chunks: pre-compiled sprites (`POSH`)

A `BLIT` chunk is a canvas whose pixels are stored **already compiled** for OMT's compiled blitter
(`OMediaCompBlitterWorld`, source `OMediaCompBlitterWorld.cpp`): a per-row program of
skip / fill / copy / masked-copy runs that draws the sprite with its transparent pixels left out.
It is Human Code's subclass of `OMediaCanvas` (database type `'BLIT'`, tag `'POSH'`), used only on
8-bit screens as a faster alternative to the `Canv` with the same name.

The header is big-endian like every OMT stream; the **compiled program is little-endian** (it is
the in-memory x86 data written out as is).

Validated by `local/re/gfx_decode.py`: **333/333 `BLIT` chunks** parse with no slack; every row
program ends exactly at the next row's table offset and the last one at the end of the program.
305 of the 308 `BLIT`s that have a `Canv` of the same name (or the name without `_<n>`) reproduce it
pixel for pixel (key pixels = empty); the other 3 are genuine art differences (§7).

## 1. Functions

| Address | Function |
|---|---|
| `0x450150` | registers the `'BLIT'` builder `0x4503c0` (priority −900) and caches the screen depth in `0x8053b0` |
| `0x4503c0` | builder: 0x34-byte object, ctor `0x450090` (derives from `OMediaCanvas` `0x45f6a0`; vtable 0x7f42f8) |
| `0x450200` (thunk `0x450460`) | canvas read: tag `'POSH'`, attributes, creates the world, reads it |
| `0x450190` (thunk `0x450470`) | canvas write |
| `0x450480` | world ctor (0x58 bytes, vtable 0x7f4348): `OMediaCompBlitterWorld` (`0x47f800`) subclass |
| `0x450630` | world read (world vtable +0xb4; reached from the virtual-base read `0x4505f0`) |
| `0x450850` | world write (vtable +0xb8) |
| `0x47fcc0` | `OMediaCompBlitterWorld::draw(dest, x, y, mask)` (world vtable +0x14) |
| `0x480420` | `drawRect(dest, srcRect, x, y, mask)` (vtable +0x20) — the clipped path |
| `0x482e50` | 8-bit opaque interpreter, whole sprite visible |
| `0x483180` | 8-bit opaque interpreter, clipped |
| `0x487ba0`, `0x488c80`, `0x486d80` | copy run, masked run, generic fill (used when `0x8294e4` ≠ 0) |
| `0x44dd90` | `HCApplication`: load a still image by name, `BLIT` first on 8-bit screens, else `Canv` |

## 2. Chunk layout

| Offset | Type | Field | Values on disc |
|---|---|---|---|
| 0 | u32 BE | tag | `'POSH'` |
| 4 | u32 BE | `attr` (world capability mask, as in `Canv`) | 0x1A (= `OMediaCompBlitterWorld`'s mask, `0x47fcb0`) |
| 8 | i32 BE | `programSize` | |
| 12 | `programSize` bytes | compiled program (§3), little-endian | |
| +0 | i16 BE | width | = program width |
| +2 | i16 BE | height | = program height |
| +4 | i16 BE | depth | 8 |
| +6 | 3 × u8 | key colour r, g, b (stored at world+0x46 as 16-bit guns, `v*65535/255`) | (255, 0, 255) |
| +9 | i16 BE | flag (world+0x4c) | 1 |

The trailer meaning is partly *inferred*: the colour is the key colour as RGB (it equals the
palette entry 254 of the scene palettes) and the flag is "has key". There is **no palette**: the
program holds raw palette indices (§6).

The world read (`0x450630`) reads `programSize` (i32), allocates a 16-byte memory-handle object
(`0x492600`) of that size, locks it (+0x0c), reads the bytes, unlocks (+0x10), then reads the
trailer. It derives the world's pixel format code (+0x34) from the depth: ≤ 8 → 1, 16 → 2 (or 4
when the screen is 5-6-5), 32 → 5. When the screen is deeper than 8 bpp the canvas read gives the
world the current node palette (`0x80a3cc`, set by `HCNode`, see [canvas.md](canvas.md) §7).

## 3. Compiled program

All fields little-endian. Offsets are from the start of the program.

| Offset | Type | Field | Values on disc |
|---|---|---|---|
| 0 | u16 | `height` (rows) | |
| 2 | u16 | `width` (pixels) | |
| 4 | u16 | `pitch` | always `(width + 7) & ~7` |
| 6 | u16 | `depth` | 8 |
| 8 | u32 | `pad` | 0 |
| 12 | `pad` bytes | padding | |
| 12 + pad | `height` × u32 | row table: offset of each row program, from the program start | row 0 starts right after the table |
| … | … | row programs | |

A row program is a sequence of **ops**. Every op is preceded by a u32 `n` and `n` padding bytes
(values 0–3; the padding bytes are garbage), which make the op header 4-byte aligned within the
program. The op header is i16 `op`, i16 `count`:

| op | Name | Size (incl. 4-byte header) | Effect at the current destination position |
|---|---|---|---|
| 0 | SKIP | 4 | leave `count` pixels untouched (transparent run) |
| 1 | FILL | 8 | u32 at +4 holds the pixel value replicated 4× (`9B 9B 9B 9B`); write `count` pixels of it |
| 2 | COPY | 4 + `count` | `count` literal pixels follow |
| 3 | MASKED | 8 + 2·`count` | u32 `k` at +4; `count` source pixels at +8, then `count` mask bytes at +8 + 4·`k` (`4k = count` on the disc). Per pixel: `dst = (dst AND mask) OR src`; mask 0x00 = opaque, 0xFF = keep destination (source is 0 there) |
| 4 | END | 2 (header is only the i16 op) | end of row |

* `count` is in bytes (= pixels at 8 bpp). In the deeper-pixel paths the run helpers divide it by
  the pixel size (`0x488c80`: `count >> 1` for 16 bpp, `>> 2` for 32 bpp) *(only 8-bit on disc)*.
* Pixels to the right of the last op of a row are transparent; rows do not need to add up to
  the width.
* MASKED runs come in multiples of 8 pixels and may extend past `width` (on the disc up to 4
  pixels past the width and 3 past the pitch); the excess is always fully masked (mask 0xFF,
  source 0), so it never changes the destination. A port must still not write past the clip
  rectangle.
* Op counts on the disc: 12 229 SKIP, 118 198 FILL, 151 875 COPY, 2 744 MASKED, 38 792 END.

## 4. Drawing (`0x47fcc0`)

`draw(dest, x, y, mask)`:

1. Lock the destination (vtable +0x48); give up if its flags (+0x38) have bit 2. Lock the program.
   Get the destination's pixels, depth and row bytes (vtable +0x64). If the destination format
   differs from the program's (vtable +0x3c check), report "Bad destination for compiled image".
2. Make `x`, `y` relative to the destination origin (dest+0x1c, +0x20). Intersect the sprite
   rectangle (`x`, `y`, `x+width`, `y+height`) with (0, 0, destWidth, destHeight). Nothing visible →
   done.
3. **Fully visible** → run the whole program on `dstPtr = pixels + y·rowBytes + x·bytesPerPixel`:
   * no blending (world+0x24 == 0) and no mask world: `0x482e50` (8-bit; deeper formats go
     through the same interpreter with `0x8294e4` = depth selecting generic run helpers);
   * blending (world+0x24 ≠ 0, level world+0x26): `0x483620` (8-bit, through the palette's RGB
     table and the blend tables at `0x8193f8 + level·1024`), `0x4841b0` / `0x484c60` (16-bit 555 /
     565), `0x485730` (32-bit) *(blending is not used by any code path traced here)*;
   * with a mask world (4th argument, depth 4 alpha) other variants (`0x48d050`, `0x48e0b0`,
     `0x48f100`, `0x490160`, …).
4. **Partly visible** → `drawRect` (`0x480420`) with the visible source rectangle; for 8-bit
   opaque it calls `0x483180(program, rows, rowBytes, dstPtr, clipLeft, firstRow, clipWidth)`:
   * start at the row-table entry `firstRow`;
   * walk each row's ops accumulating `x`; ops that end at or before `clipLeft` are skipped whole;
     the op that crosses `clipLeft` is entered at offset `clipLeft − x` (source and mask pointers
     advanced by the same offset);
   * from there each op is cut to the remaining `clipWidth`; stop at END or when the width is used up;
   * advance the destination by `rowBytes` per row.
5. Unlock program and destination.

8-bit opaque interpreter, per row (`0x482e50`):

```
d = rowStart
loop:
    p += 4 + u32(p)                 # skip padding
    op = i16(p)
    if op == 4: p += 2; break
    n = i16(p + 2)
    if op == 0: d += n;                         p += 4
    if op == 1: fill d[0..n) with u8(p + 4);    d += n; p += 8
    if op == 2: copy n bytes from p + 4 to d;   d += n; p += 4 + n
    if op == 3: for i < n: d[i] = (d[i] & mask[i]) | src[i]   # src = p + 8, mask = src + 4*u32(p + 4)
                d += n; p += 8 + 2n
rowStart += rowBytes
```

## 5. Relation to `Canv`

* A `Canv` with `attr` 0x1A is loaded into the same `OMediaCompBlitterWorld` class and compiled
  at load time; a `BLIT` is that world saved in compiled form, so the game skips the compile.
* `0x44dd90(name, preferBlit)` (used by `HC2DAnim` frame loading `0x448320`/`0x4485b0`, the
  `HCNode` background sprite `0x454ad0` and the race node `0x422d00`, always with `preferBlit` = 1)
  loads `BLIT name` when the screen is 8 bpp, otherwise or if missing `Canv name`.
* The pairing is **by name, not by id**: ids are assigned per type and differ (e.g. tackroom `Canv`
  1 = `BLIT` 0 = `EnglishSaddle_1`).
* 305 of 333 `BLIT`s have a same-name `Canv`; 3 more (`preferences background_1`,
  `vinegame background_1`, `Bellgame Background_1`) match the `Canv` named without `_1`
  (case-insensitively); the 25 `MouthFlapStable_<n>` frames in `stable.omt` exist only as `BLIT`.

## 6. Colours

`BLIT` pixels are **raw indices into the display palette** and are never remapped (in 8 bpp the
world has no palette; only on deeper screens does it get the node palette for conversion).
`HCNode` sets the display palette to its background's palette ([canvas.md](canvas.md) §7), so a
`BLIT` must be drawn in a scene whose palette it was made for. That is why `common.omt` holds the
shared UI twice, `…In_<n>` (indoor palettes) and `…Out_<n>` (outdoor palette). To view a `BLIT`
outside the game, use the palette of its same-name `Canv`, else of the scene's background.

## 7. Differences from the same-name `Canv`

| File | Name | Differing pixels | Region |
|---|---|---|---|
| grooming.omt | `grooming background` | 8 992 | (0,34)–(638,400) |
| stable.omt | `stable background` | 2 829 | (1,184)–(622,334), small retouches |
| tackroom.omt | `pad2g1` | 12 | (0,4)–(77,64) |

In 8-bit mode the game shows the `BLIT` version.

## 8. Decoding to an image (as `gfx_decode.py` does)

Start each row filled with the key index (254, or the same-name `Canv`'s key), interpret the ops
over a buffer `max(width, pitch, longest row)` wide, then crop to `width`. MASKED pixels with mask
0xFF and SKIP runs stay key. Write an indexed PNG with the palette chosen as in §6 and the key index
transparent: `local/extract/<omt>/BLIT_<id>_<name>.png`.
