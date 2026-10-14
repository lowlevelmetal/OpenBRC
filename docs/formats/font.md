# Bitmap fonts (canvas fonts)

The game draws text with OMT 1.x's **canvas font** (`OMediaCanvasFont`): the glyphs are stored as
one ordinary `Canv` chunk ([canvas.md](canvas.md)) — a strip **one cell wide and 225 cells tall**,
one glyph per cell, top to bottom — and the font object knows the cell size, a character → cell
table and per-cell widths. There is no font chunk type; the font parameters are hard-coded where
each font is created.

## 1. Font canvases on the disc

| Name (mCTy) | File | Size | Depth | Cell w × h | Cells | Notes |
|---|---|---|---|---|---|---|
| `BureauGrotesque-FiveThree_18` | common.omt `Canv` 1 | 37 × 6525 | 32 | 37 × 29 | 225 | attr 0x22D |
| `BureauGrotesque-FiveThree_10` | common.omt `Canv` 0 | 20 × 3825 | 32 | 20 × 17 | 225 | attr 0x22D |
| `BureauGrotesque-FiveThree_18bb` | bulletinboard.omt `Canv` 0 | 37 × 6525 | 32 | 37 × 29 | 225 | same pixels as `_18`, attr 0x22F |
| `Lucida_Console_10` | Debug.omt `Canv` 0 | 12 × 4050 | 1 | 12 × 18 | 225 | debug overlay only |
| `font` | common.omt `Canv` 20 | 8 × 1092 | 8 | 8 × 12 | 91 | no reference found in the executable *(unused)* |

* The 32-bit fonts are **black glyphs anti-aliased on white**: nine values 0x000000, 0x1F1F1F,
  0x3F3F3F, … 0xDFDFDF and 0xFFFFFF. White is the transparency key (`hasKey`, key 0x00FFFFFF).
* Cell `i` covers rows `i·h … i·h + h − 1`; glyphs are **left-aligned and top-aligned** in their
  cell (there is no baseline field; all glyphs of a font share the cell's top line).
* Cell order (225-cell fonts): cell 0 is an empty box (the glyph for unmapped characters),
  cell `c − 31` is character `c` for `c` = 0x20…0xFF: ASCII for 0x20–0x7E; 0xC0–0xFF are the
  Latin-1 accented letters; 0x80–0xBF mostly follow Windows-1252 (`ƒ „ … † ‡ ‰ ‹ Œ ™ › œ Ÿ ¡ ¢ £`),
  with empty boxes or substitutes in several cells (e.g. 0x80, 0x88, 0x8A, 0x8E).
* The 91-cell `font` canvas is in the order of OMT's default inverse table
  `"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789().;,!?:{}+-=[]#@ ©$\"/><'&%*¡"`
  (string at 0x7ffa08, also not referenced by code).

## 2. The font object (`OMediaCanvasFont`, 0x314 bytes)

Constructor `0x469030`, vtable 0x7f51a8.

| Offset | Type | Field | Default |
|---|---|---|---|
| +0x00 | ptr | vtable | |
| +0x04 | ptr | font canvas (its reference count at canvas+0x14 is kept) | null |
| +0x08 | i16 | `fwidth` (cell width) | 0 |
| +0x0A | i16 | `fheight` (cell height) | 0 |
| +0x0C | u8[256] | `trans`: character → cell index | identity |
| +0x10C | i16 | `proportional` | 0 |
| +0x10E | i16[256] | `width[cell]` (proportional table) | |
| +0x30E | i16 | `spaceSize` | 0 (set to `fwidth` by `setFontCanvas`) |
| +0x310 | i16 | `charSpace` (gap after each glyph) | 2 |
| +0x312 | i16 | `verticalDelta` (distance between cell tops) | 0 |

| vtable | Address | Method |
|---|---|---|
| +0x04 | `0x4690c0` | `setCanvas(canvas)` |
| +0x08 | `0x4690e0` | `setFontCanvas(canvas, fwidth, fheight, verticalDelta)`; also sets `spaceSize = fwidth`; `verticalDelta = −1` means `fheight` |
| +0x0C | `0x469130` | `setTranslationTable(u8* table, n)`: copies `min(n, 256)` bytes |
| +0x10 | `0x469740` | `setProportionalTable(i16* table, n)` |
| +0x14 | `0x469770` | `createProportionalTable(const RGB48* background)` |
| +0x18 | `0x469160` | `drawString(string, dest, x, y)` |
| +0x1C | `0x469360` | `drawString` with a clip rectangle |
| +0x20 | `0x469950` | `textWidth(string)` |

## 3. How the game sets fonts up

Every user creates a small Godiva wrapper (0x14 bytes; base vtable 0x7f0d98, `getFont` +4 =
`0x40cd30`, `getCanvas` +8 = `0x40cd40`) holding the canvas (+4, loaded with
`0x44dd10('Canv', name)`), the font (+8) and an RGB colour (+0xC) set to white (255, 255, 255 →
65535 each). Then:

| Font | `setFontCanvas` | table | `spaceSize` | `charSpace` | `proportional` |
|---|---|---|---|---|---|
| `…_18`, `…_18bb` | (canvas, 37, 29, −1) | 0x7f12d0 / 0x7f14e0 | 12 | 1 | 1 |
| `…_10` | (canvas, 20, 17, −1) | 0x7f13d8 | 6 | 1 | 1 |
| `Lucida_Console_10` | (canvas, 12, 18, −1) | built on the stack | | | |

The three static tables are identical: `trans[c] = 0` for c < 0x20 and `trans[c] = c − 31` for
c ≥ 0x20. Finally `createProportionalTable(&wrapper.colour)` (white) is called.

## 4. Proportional widths (`0x469770`)

```
maxW = min(canvasWidth, fwidth)
for top = 0, verticalDelta, 2·verticalDelta, … while top + fheight <= canvasHeight and top / verticalDelta < 256:
    cell = top / verticalDelta
    widest = 0; found = false
    for row in 0 .. fheight-1, x in 0 .. maxW-1:
        if pixel(x, top + row) != background:      # compared as 16-bit RGB guns
            found = true; widest = max(widest, x)
    width[cell] = found ? widest + 1 : spaceSize
```

Glyph pixels are read from the loaded canvas world (`0x46b5d0`, get pixel as RGB). At that point
the 32-bit canvas has already been converted to the 8-bit screen format ([canvas.md](canvas.md)
§6), so white must still read back as pure white for the widths to come out right *(inferred;
a port can compute the table directly from the 32-bit canvas, treating 0xFFFFFF as background)*.
Results for the 18-pt font: cell 0 → 19, `!` → 5, `"` → 11, `#` → 17, …; 10-pt: 10, 4, 6, 11, ….

## 5. Drawing and measuring

`drawString(text, dest, x, y)` (`0x469160`) — `x`, `y` are the **top-left** of the text box:

```
for each byte c of text:
    if proportional:
        if c == ' ': x += spaceSize; continue
        cell = trans[(signed char)c]
        src  = rect(0, cell·verticalDelta, width[cell], cell·verticalDelta + fheight)
        canvas.drawRect(dest, src, x, y, 0)          # canvas vtable +0x20
        x += width[cell] + charSpace
    else:
        cell = trans[(signed char)c]
        src  = rect(0, cell·verticalDelta, fwidth, cell·verticalDelta + fheight)
        canvas.drawRect(dest, src, x, y, 0)
        x += fwidth                                  # no special case for space, no charSpace
```

`textWidth(text)` (`0x469950`): non-proportional → `fwidth · length`. Proportional → sum over the
characters of `spaceSize` for a space, else `width[cell]`, plus `charSpace` after every non-space
character **except the last character of the string**.

Details for a port:

* The character is sign-extended before indexing `trans`, so bytes ≥ 0x80 index *before* the
  table (into the object's own fields) — a bug in the original; game text is plain ASCII.
* There is no colour parameter: glyphs are copied with the canvas's transparency key, so text
  always appears in the font's own black/grey, mapped to the nearest colours of the display
  palette when the font canvas was loaded. There is no kerning and no line breaking in the font.
* `drawRect` clips to the destination like any canvas copy ([canvas.md](canvas.md) §8).

## 6. Who draws text

| Where | Font | What *(content partly inferred)* |
|---|---|---|
| signin ctor `0x427370` | `_18` (wrapper at node+0x1CC) and `_10` (node+0x1D0) | sign-in screen names |
| bulletinboard `0x40c960` (from `0x40e410`) | `_18bb` (bulletinboard.omt) | membership card close-up (`membershipcards`, `bb_membership`, `bbcloseup`): a text element with the string at (node+0x1B8)→+0x1B0, i.e. the player's name *(inferred)* |
| yard ctor `0x43cda0` → `0x43e930` | `_18` | text element with the same string, centred at x = 320 − w/2, y = 270, max width 300 |
| `0x434610` (from `0x43efc0`, `0x43f5a0`) | `_18` | same text element pattern |
| debug overlay `0x455e10` (only if registry `Debug Display` ≠ 0; opens `debug.omt`) | `Lucida_Console_10` | debug text |

The text element used by the yard pattern is an OMT animation element (base ctor `0x465d10`,
0x168 bytes, Godiva vtable 0x7f0ca8): font at +0x124, text set with vtable +0x98 (`0x466030`),
size with +0x5C (`0x4660e0`); +0xB4 (`0x466d40`) sets its width to
`textWidth + margin(+0x13C) + 2·border(+0x100)`, clamped to +0x13A.
