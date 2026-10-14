# Smacker: which features the game's data and code use

What an OpenBRC Smacker decoder and player must support, measured on every Smacker stream on the
disc and on the game's calls into `smackw32.dll` (Smacker 3.2f). The container layout itself is the
public Smacker format (header, frame-size table, frame-type table, Huffman trees, frames); this page
only states which parts occur. Player behaviour is in `../engine/movies.md`, sprite use in
`../engine/animation.md` §6 and `hcanim.md`.

Validation: `local/re/snd_smkscan.py` parses the header, both tables and every frame's chunk layout
(palette record, audio chunks, video remainder) of **967/967 streams**: 253 standalone `.smk` files,
275 `SMK2` chunks and 439 `SMAK` chunks (all `.omt` and `.omt.cache` files), with 0 errors. For every
stream the walked frames end exactly at the end of the file or chunk (no trailing data).

## 1. Inventory

| Source | Count | Notes |
|---|---|---|
| `Resources/*.smk` | 4 | `DrinkDpl/Pal/Pnt/Wht.smk` 640×480, 85 frames, 15 fps |
| `Resources/SMK/*.smk` | 81 | intro, payoff, carousel, door, bell game, feed close-ups, trail segments and overlays (400×300 and `_L` 300×152), `deer.smk` 464×164 |
| `Resources/SMK/HorseHeads/*.smk` | 60 | `_crop` (300..412 × 332..428), `_L` (300..412 × 244..340), `_CropHalf` (half size, unused) |
| `Resources/SMK/butterflies/*.smk` | 108 | 16×16 … 100×112, 2 frames each |
| `SMK2` chunks | 275 | sprite animations, 20×20 … 640×480, 1–466 frames (85 single-frame) |
| `SMAK` chunks | 439 | cropped sprite animations (with `SMKI` sizes), 12×12 … 640×480, 1–199 frames (180 single-frame) |

## 2. Header fields

| Field | Values on the disc | Decoder requirement |
|---|---|---|
| Signature | **`SMK2` in all 967** — no `SMK4` | SMK2 video only. (The HCMovie header probe `0x452f60` accepts `SMK2` and treats `SMKa`/`SMKb` as an empty movie; the sprite loader accepts `SMK2`/`SMK1`.) |
| Width × height | all widths multiple of 4, all heights even | — |
| Frames | 1 … 1132 (RabbitE.smk) | — |
| FrameRate (s32) | `100` (100 ms → 10 fps): 132 files + all 714 chunks; `-10000` (10 fps, units of 10 µs): 44 files; `-6666` (66.66 ms → 15.0 fps): 77 files (Drink*, deer, 72 butterflies) | positive = ms/frame, negative = −value/100 ms. The game overrides the rate with `SmackFrameRate` for SmackMovie/trail players (12/14/10 fps) |
| Flags | **0 in all 967**: no ring frame, no Y-interlace, no Y-doubling | none of these needed |
| AudioRate[0] | `0xC0005622` (12 files), `0xD0005622` (2 files), 0 elsewhere | see §5; tracks 1–6 never used |
| TreesSize | 333 … 173412 bytes | — |
| MMap/MClr/Full/Type tree sizes | max 253496 / 147464 / 240840 / 21104 | normal SMK2 Huffman tree decoding |

## 3. Frame tables

* Frame-size bit 0 ("keyframe") and bit 1 are **never set** (0 of 27455 frames).
* Frame-type bit 0 (palette record): **only in frame 0, in every stream**. Bits 1+ (audio): only bit 1
  (track 0), only in the 14 files with audio.
* So every stream is a single palette plus delta video from frame 0. Seeking (`SmackGoto`, used for
  sprites and the trail) must decode forward from frame 0 *(inferred: no keyframes to start from;
  sprites preload the whole stream and step frame by frame, so this is cheap)*.

## 4. Palettes

* One palette record per stream, 196 … 768 bytes. The records use only two of the three palette
  commands: 6-bit RGB triples (224505 entries in total) and "skip n entries" (`0x80|n−1`, 1213 times;
  in frame 0 a skipped entry keeps the initial all-black value). The "copy n entries from index"
  command (`0x40|n−1`) never occurs. 6-bit components expand with `(v·255 + 31) / 63` (0, 4, 8, …,
  65, …, 255), the same table as the DLL's (BarbieRecomp §6.5).
* Palette conventions found in the files (entry 0 is black in all 967):

| Entry | Sprites (SMK2/SMAK, 714) | HCMovie/SmackMovie files | Trail segment and overlay files |
|---|---|---|---|
| 254 (0xFE) | **magenta 255,0,255** in 713 (one SMAK: cyan) — the sprite transparent index | magenta in BellMH*, MH*, Carousel, OpenDoor, Drink*, FeedCloseUp*, ApplePickAnim; **not** in Intro (0,0,24) or MHP (0,0,8) | magenta in most full-size files; black in the `_L` files (except `aaW FallenBird_L`), CottageW and the trE files |
| 255 | white in 710 (4 × 251,255,255) | white | white, except black in the same `_L` files |

  The trail player ignores the SMK palettes (it installs a `DATA` palette, `../engine/movies.md` §4.2),
  so their differences do not matter; HCMovie's magenta colour key therefore makes nothing
  transparent in the intro and payoff movies *(inferred)*.

  Only entries 0 and 255 coincide with the Windows static colours; the other 18 system entries are
  ordinary picture colours.
* The game never uses a palette from a frame other than frame 0 (none exist). Trail movies ignore
  their SMK palette and use a `DATA` palette chunk instead (`../engine/movies.md` §4.2); sprites
  ignore theirs too (`animation.md` §11).

## 5. Audio

| Property | Value |
|---|---|
| Files with audio | 14: Intro, MHP (stereo); ApplePickAnim, BellMH1–3, MH1–7, OpenDoor (mono) |
| Track | 0 only |
| Format | 22050 Hz, **8-bit**, Smacker DPCM ("compressed", bit 31; codec bits 26–27 = 0 — no Bink audio), stereo bit 28 for Intro/MHP |
| Pre-roll | frame 0 carries 1.10 s of audio (24256 bytes mono / 48512 stereo unpacked = the AudioSize field), then 0.1 s per frame; the last 10 frames carry none |
| Length | audio length = video length for all 14 (e.g. Intro 42.0 s, MHP 75.7 s) |
| Silence | silent frames carry 12-byte audio chunks (length word, unpacked-size word, 4 bytes of bitstream) |

Audio plays through Miles (`SmackSoundUseMSS`); OpenBRC can decode it to PCM and feed its mixer,
pacing video against the audio clock as Smacker does *(inferred)*. Of the 14, only Intro, MHP,
BellMH1–3, MH7 and OpenDoor are played (`../engine/movies.md` §4.4).

## 6. Player features the code uses

| Feature | Where | Needed by OpenBRC as |
|---|---|---|
| `SMACKTRACKS` (0xFE000) on every movie open | all movie players | play track 0 audio when present |
| `SMACKPRELOADALL` | OpenDoor (both players), FeedCloseUp*, trail overlays (horse heads, deer, butterflies, event overlays), sprites; not the trail main movie or back loops | load the whole stream |
| `SMACKNOSKIP` | most HCMovies; sprites | never drop frames to catch up |
| `SMACKLOADEXTRA` 2300000 | bell-game movies | (memory hint only) |
| `SMACKFRAMERATE` + `SmackFrameRate(fps)` | SmackMovie/trail (12, 14, 10 fps) | override the file's frame rate |
| `SMACKFILEHANDLE` (0x1600) | sprites from .omt/.cache | read from a byte range of the container; leave the file position at the stream end |
| `SmackGoto(n)` (1-based) | sprite frames, trail loops/restarts | random access |
| `SmackToBuffer` pitch = width | movies | packed 8-bit frame buffers |
| `SmackToBufferRect` dirty rectangles | HCMovie, sprites in renderer mode 3 | optional optimisation |
| `SmackBlit` 1× and 2× (8-bit) | trail (always 2×), HCMovie double mode (unused) | nearest-neighbour 2× scaling, half-resolution destination offsets |
| `SmackBlitTrans` with a transparent index | trail overlays (index 0xFE) | colour-keyed copy |
| `SmackSummary` | intro perf test, trail logging | TotalTime, decompression/blit/read times, SoundSkips (`../engine/movies.md` §2) |

Not needed: SMK4 features, ring frames, interlaced/doubled flags, 16-bit or Bink audio, more than one
audio track, palette changes after frame 0, keyframes.
