# SCR — compiled Human Code scripts (and the DATA palettes next to them)

Status: verified against all 54 `SCR ` chunks and all 13 `DATA` chunks on the disc with
`local/re/scr_disasm.py` (54/54 SCR chunks decode and pass every check listed in
[Validation](#validation); 13/13 DATA chunks are 768-byte palettes).
Runtime behaviour (interpreter, built-ins, scheduling, node integration) is in
[`docs/engine/scripts.md`](../engine/scripts.md).

Addresses refer to `Barbie Riding Club.exe` (image base 0x400000). Source files named in
the executable's assert strings: `E:\Projects\Omt\Godiva\hcscript.cpp`, `exec.cpp`,
`stack.cpp`.

## Where scripts live

| File | SCR chunks (id: name) |
|---|---|
| appleW.omt | 0 appleestop, 1 slowdown |
| applesort.omt | 0 flippers |
| carousel.omt | 0 CarouselWalk, 1 Quilt (empty: 0 code bytes) |
| common.omt, commonhd.omt | 0 Delay, 1 DelayVoice, 2 GameAmb, 3 preload (identical copies) |
| credits.omt | 0 credits |
| hclogonode.omt | 0 music |
| leapfrog.omt | 0 Croak, 1 Ducks, 2 delayJump |
| race.omt | 0 raceabort, 1 raceabortgo, 2 racecjump, 3 racecrun, 4 racecstart, 5 racejump, 6 racerun, 7 racestart |
| tackroom.omt | 0 TackRoom, 1 TackRoomHorseWalk, 2 TackRoomHorseWalkPal, 3 TackTour |
| trails.omt | 23 scripts (ExcitedLook, LookLeft, M5Payoff, PagerDelay, RunRight, TMoment2, WalkBack, WalkLookLeft, WellSquirrel, ambient, ambientwait, bridgeFrog, dismount, feedapple, horseamb, loadwalk, m6_rabbit, mount, runleft, runoff, runon, walk, walkamb) |
| yard.omt | 0 intro |

Scripts are always found **by name**, never by id: `HCScript` (ctor 0x458fb0) calls the
application's resource lookup 0x44dca0 with type `'SCR '` (0x53435220) and the script name.
That walks the open OMT files in order and maps the name to a chunk id through each file's
`mCTy` name table, comparing case-insensitively (`_strcmpi`, 0x451020). C++ code uses
names such as `"raceAbort"`, `"Flippers"`, `"Intro"` for the chunks `raceabort`, `flippers`,
`intro`.

`mCTy` layout (only what the disassembler needs): BE u32 type count; per type: fourcc,
BE u16 (0 or 1, meaning unknown), BE u32 entry count, then entries of BE i32 chunk id,
u8 name length, name bytes. Some `mCTy` chunks have unused bytes after the last entry.

## Chunk layout

All SCR fields are **little-endian** (the executor reads them as native x86 dwords), unlike
the big-endian OMT container.

| Offset | Size | Field |
|---|---|---|
| 0x00 | u32 | Version. Must be 5 (compared at 0x44587b). Otherwise the executor logs `Script executor error: executor version 5, script compiled with compiler version %ld` and the script never runs. All 54 chunks: 5. |
| 0x04 | u32 | `symoff`: offset of the symbol table from the chunk start. The code section is `[0x08, symoff)`. |
| 0x08 | symoff−8 | Code: a stream of instructions (below). 0 to 598 bytes on the disc. |
| symoff | u32 | `nsym`: number of symbols. |
| symoff+4 | … | `nsym` symbol records. |
| … | … | Slack: 7 chunks have 3–513 bytes of garbage after the last record (left over from earlier, longer versions of the chunk; `credits` still contains an older symbol table). The loader never reads it. |

There is no separate constant pool or string pool: every string (variable names and string
literals) is a symbol, and float constants are inline instruction operands.

### Symbol record

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | `id`. The executor stores the symbol at `table[id]` (0x445924), so ids must be a permutation of 0..nsym−1. Records are **not** sorted by id (e.g. `credits`: 4, 0, 1, 3, 2, 5). |
| 4 | u32 | `type`: 0 = float variable, 1 = string constant, 2 = string variable. Anything else makes the loader fail with `Script executor error: error loading symbol table` (0x45bb50 returns −1). |
| 8 | u32 | `len`: byte length of the text that follows (no terminator in the file). |
| 12 | len | Text: the variable name (types 0/2) or the literal string itself (type 1). Copied into a 32-byte field; the longest on the disc is 19 bytes. Lengths ≥ 32 would overflow into the value field — not present on the disc. |

Record size = 12 + len (that is what 0x45bb50 returns). All variables start as 0.0 / `""`
(the symbol object is zeroed by 0x45ba60). Counts on the disc: 216 string constants,
71 float variables, 29 string variables.

Note: C++ code can change a symbol's value **and type** at run time (string assignment sets
type 2, float assignment sets type 0) — see the engine document.

## Instruction encoding

Each instruction is a little-endian **signed 16-bit opcode**, optionally followed by one
operand:

| Operand kind | Size | Used by |
|---|---|---|
| `f32` | 4 | opcode 1 (IEEE single, little-endian) |
| `i16` | 2 | opcodes 2, 3, 4 (signed) |
| none | 0 | everything else |

All other arguments come from the **value stack** (30 slots of 4 bytes, see the engine
document). A slot holds either a float (`f`) or a 16-bit integer in the low half (`s`,
symbol index or code offset). The interpreter never checks slot types; the compiler always
pairs them correctly (verified statically, see Validation).

**Jump targets are offsets from the start of the chunk** (not from the code section):
`pc = chunk_base + target` (JZ 0x445b92, JMP 0x445bcf). Valid targets are instruction starts or `symoff`
(end of code = end of script). One chunk on the disc jumps to offset 0, see
[Quirks](#quirks-in-the-shipped-data).

### Opcode table

Stack notation: arguments are listed in **push order** (left = pushed first = deepest);
`→ x` is the pushed result. `f` float, `s` 16-bit symbol index, `L` 16-bit code offset.
"Used" = number of occurrences in the 54 chunks (2450 instructions in total).

Core opcodes (dispatcher 0x445a10, arithmetic/logic 0x445c60, strings 0x446290):

| Op | Name (ours) | Operand | Stack | Semantics | Used |
|---|---|---|---|---|---|
| 0x01 | PUSHF | f32 | → f | push the constant | 766 |
| 0x02 | PUSHI | i16 | → s | push the immediate; the compiler uses it only for jump targets | 171 |
| 0x03 | PUSHSYM | i16 | → s | push a symbol index (identical at run time to 0x02); −1 = "no symbol" for optional arguments | 471 |
| 0x04 | LOAD | i16 | → f | push the 4-byte value field of symbol `n` (meaningful for float variables only; the compiler never uses it on strings) | 134 |
| 0x05 | POPF | — | x → | discard the top slot (the compiler also uses it after string assignments) | 129 |
| 0x06, 0x07 | NOP | — | — | nothing | 0 |
| 0x08 | JZ | — | f L → | pop target, pop condition; if condition == 0.0 jump | 81 |
| 0x09 | JMP | — | L → | pop target, jump | 90 |
| 0x0a | POPF2 | — | f → | discard (same as 0x05) | 0 |
| 0x0b | POPS | — | s → | discard a 16-bit slot | 0 |
| 0x0d | ASSIGN | — | f s → f | `sym = value` (float, symbol type becomes 0); pushes the value | 99 |
| 0x0e | ADD | — | a b → a+b | float | 15 |
| 0x0f | SUB | — | a b → a−b | | 17 |
| 0x10 | MUL | — | a b → a·b | | 0 |
| 0x11 | DIV | — | a b → a/b | float division (÷0 gives ±inf/NaN, no check) | 1 |
| 0x12 | MOD | — | a b → a mod b | `(float)(trunc(a) % trunc(b))` with C `%` (sign of a); b truncating to 0 crashes the original | 0 |
| 0x13 | NEG | — | a → −a | | 5 |
| 0x14 | POW | — | a b → aᵇ | CRT `pow` (0x7dcdc0) | 0 |
| 0x15 | RAND | — | n → r | `rand() % trunc(n) + 1`, i.e. 1..n (MSVC `rand`, 0x7dc460) | 20 |
| 0x16 | SIN | — | a → sin a | radians (x87 `fsin`) | 0 |
| 0x17 | COS | — | a → cos a | | 0 |
| 0x18 | SQRT | — | a → √a | | 0 |
| 0x19 | EQ | — | a b → a==b | results are 1.0 / 0.0 | 31 |
| 0x1a | NE | — | a b → a!=b | | 0 |
| 0x1b | OR | — | a b → a‖b | 1.0 if either is non-zero | 3 |
| 0x1c | AND | — | a b → a&&b | | 0 |
| 0x1d | NOT | — | a → !a | 1.0 if a == 0 | 3 |
| 0x1e | GT | — | a b → a>b | | 1 |
| 0x1f | GE | — | a b → a>=b | | 9 |
| 0x20 | LT | — | a b → a<b | | 3 |
| 0x21 | LE | — | a b → a<=b | | 0 |
| 0x24 | SASSIGN | — | src dst → dst | `dst = text(src)` (string, type becomes 2); pushes dst's index | 29 |
| 0x25 | SCONCAT | — | a b dst → dst | `dst = text(a) + text(b)`, truncated to 31 characters | 0 |
| 0x26 | NUM2STR | — | f dst → dst | `dst = sprintf("%-*f", 31, f)` then trailing spaces/zeros and a trailing `.` removed (3.0 → `"3"`, 2.5 → `"2.5"`) | 0 |
| 0x27 | STRCMP | — | a b → f | −1/0/+1 by unsigned byte comparison of text(a) and text(b) | 0 |
| 0x28 | STRICMP | — | a b → f | CRT `_strcmpi(text(a), text(b))` converted to float (0 = equal) | 2 |
| 0x00, 0x0c, 0x22, 0x23, 0x29, 0x2a, 0x3a, 0x3b, 0x46, 0x47, 0x52, 0x53, ≥0x66, <0 | invalid | — | — | logs `Bad error: unknown opcode %ld.` and ends the script (pc = end) | 0 |

`text(sym)` is the symbol's literal for type 1 and its 32-byte value field otherwise.

Built-in opcodes (handlers 0x446590 for 0x2b–0x39, 0x446f20 for 0x3c–0x45, 0x446c50 for
0x48–0x51, 0x447280 for 0x54–0x65). They pop all their arguments and push nothing. Float
arguments are converted to int by truncation (`_ftol`) unless marked `float`. The bytecode
contains no built-in names (the symbol table holds only variables and string literals), so
the names are ours; what each one does is in the engine document. "nop" = the handler pops the
arguments and calls an empty method of `HCScript` (`ret N` stubs at 0x458bb0, 0x458ce0,
0x458cf0, 0x458d00, 0x458f90, 0x458fa0, 0x49d710, 0x49d720) — unimplemented in this
build.

| Op | Name | Arguments (push order) | Used |
|---|---|---|---|
| 0x2b | playAnim | f wait, s anim, f seq | 146 |
| 0x2c | playAnimAt | f wait, s anim, f seq, f x, f y | 5 |
| 0x2d | nop | f s f f f | 0 |
| 0x2e | nop | f s f s | 0 |
| 0x2f | nop | f s f f f | 0 |
| 0x30 | nop | f s f s f f | 0 |
| 0x31 | waitAnim | s anim, f seq, f (unused), s (unused, may be −1) | 1 |
| 0x32 | hideAnim | s anim | 3 |
| 0x33 | killAnim | s anim | 1 |
| 0x34 | nop | s s f f f f | 0 |
| 0x35 | copyAnim | s resource, s newName, f x, f y, f z | 0 |
| 0x36 | nop | s f f f | 0 |
| 0x37 | nop | (none) | 0 |
| 0x38 | moveAnim | s anim, float dx, float dy, float dz | 7 |
| 0x39 | placeAnim | s anim, float x, float y, float z | 0 |
| 0x3c | nop | f s f f f f | 0 |
| 0x3d | nop | f f | 0 |
| 0x3e | playSound | f wait, s sound, f channel, f volume, f queue, f preempt | 49 |
| 0x3f | waitSound | s sound | 5 |
| 0x40 | stopSound | s sound | 0 |
| 0x41 | stopChannel | f channel | 0 |
| 0x42 | stopAllSounds | (none) | 0 |
| 0x43 | nop | f s s s f (4th may be −1) | 0 |
| 0x44 | nop | s | 0 |
| 0x45 | nop | (none) | 0 |
| 0x48 | nop | s | 0 |
| 0x49–0x4d | nop | s f | 0 |
| 0x4e | nop | s | 0 |
| 0x4f | nop | s s f f (its handler falls through into 0x50's code) | 0 |
| 0x50, 0x51 | nop | s | 0 |
| 0x54 | nop | f f | 0 |
| 0x55 | nop | f | 0 |
| 0x56 | runScript | f wait, s script | 0 |
| 0x57 | waitScript | s script | 1 |
| 0x58 | stopScript | s script | 0 |
| 0x59 | stopAllScripts | (none) | 0 |
| 0x5a | nop | s f | 0 |
| 0x5b | setKillWithNode | f flag | 0 |
| 0x5c | setFlag36 | f flag (stored, never read) | 0 |
| 0x5d | nodeMessage | s message | 89 |
| 0x5e | nodeNumber | f value | 0 |
| 0x5f | wait | f milliseconds | 61 |
| 0x60 | gotoNode | s node | 2 |
| 0x61 | pauseNode | f flag | 0 |
| 0x62, 0x63 | nop | s | 0 |
| 0x64 | nop | f s f f f f f f | 0 |
| 0x65 | nop | s f f f f f f f f | 0 |

33 of the 90 defined opcodes occur on the disc; an implementation must still accept all of
them (with the stack effects above) to match the original.

## How the compiler emitted code

Inferred from the data; useful to read listings.

* Expression statements: assignments compile to `value, PUSHSYM var, ASSIGN, POPF`
  (string assignment: `PUSHSYM src, PUSHSYM dst, SASSIGN, POPF`).
* `if c … [else …]`: `c, PUSHI Lelse, JZ, …then…, PUSHI Lend, JMP, Lelse: …`. A `then`
  branch often ends in a `JMP` to the very next instruction.
* `while c …`: `Ltop: c, PUSHI Lend, JZ, …, PUSHI Ltop, JMP, Lend:`; `while 1` loops keep
  the constant test (`PUSHF 1, PUSHI Lend, JZ`).
* `for i = a to b`: `a → i; JMP Ltest; Linc: i = 1 + i; Ltest: b >= i …`.
* Built-in calls push their arguments left to right; the wait flag (when a built-in has one)
  is always pushed first.
* The stack is empty at every statement boundary and at every jump (verified).

Excerpt (common.omt SCR 0 `Delay`, complete; our disassembler's format, offsets from the
chunk start, `[n]` = stack depth before the instruction):

```
  0008 [ 0] 01 00 00 00 00 00  PUSHF      0
  000e [ 1] 03 00 00 00        PUSHSYM    0  ; delay
  0012 [ 2] 0d 00              ASSIGN
  0014 [ 1] 05 00              POPF
  0016 [ 0] 03 00 01 00        PUSHSYM    1  ; "initDelay"
  001a [ 1] 5d 00              nodeMessage
  001c [ 0] 04 00 00 00        LOAD       0  ; delay
  0020 [ 1] 5f 00              wait
```

that is, `delay = 0; nodeMessage("initDelay"); wait(delay)`. The node answers
`initDelay` by setting the variable `delay` from C++ before `wait` runs.

## Quirks in the shipped data

* **trails.omt SCR 3 `PagerDelay` jumps to offset 0.** Its `if … else … endif` was
  compiled with the `else`-exit jump left unpatched (`PUSHI 0, JMP` at 0x6e) and `endif`
  became a float variable (`LOAD endif, POPF`). At run time the jump lands on the chunk
  header: bytes `05 00` execute as POPF on an empty stack (logs `script stack underflow!`,
  no other effect), then `00 00` is opcode 0 → `Bad error: unknown opcode 0.` and the script
  ends. So when `delay > 250` the script waits `delay − 250` ms and ends without ever
  sending `PagerOKToGo`. A faithful VM must allow jumps to any chunk offset and decode
  whatever is there.
* carousel.omt SCR 1 `Quilt` has no code and no symbols (ends immediately when run).
* `appleestop` passes symbol −1 as the unused 4th argument of `waitAnim`.

## Validation

`python3 local/re/scr_disasm.py` decodes every SCR chunk on the disc and writes listings
(symbol table, instructions with stack depth, reconstructed pseudo-source) to
`local/extract/scripts/<omt>_<id>_<name>.txt`. For each chunk it checks: version 5;
symbol table bounds, unique ids < nsym, types 0–2, name length ≤ 31; every opcode known
and every operand inside the code; every jump target preceded by an immediate push and
landing on an instruction or `symoff`; a typed stack simulation along every control-flow
path (no underflow, depth ≤ 30, float/16-bit slot kinds match each opcode's signature,
same stack at every merge point, empty stack at end of code); and every instruction
reachable. Result: **54/54 chunks pass**, 0 unknown opcodes, maximum stack depth 6, with the
one `PagerDelay` jump to offset 0 reported as a quirk.

## DATA chunks (trails.omt): palettes, not scripts

The 13 `DATA` chunks (ids 0–12: `aa_west.act`, `cw_loop.act`, `dpl.act`, `ob_south.act`,
`pal.act`, `pal_outside.ACT`, `pnt.act`, `rf_south.act`, `rr_east.act`, `tr_east.act`,
`wht.act`, `ww_north.act`, `ww_south.act`) are Adobe Photoshop `.act` colour tables:
exactly 768 bytes = 256 × (R, G, B), 8 bits each, entry 0 first.

Reader 0x431200 (`TrailNode`): looks the name up as type `'DATA'` (0x44415441); if the
chunk exists and its size is exactly 0x300 it reads it, otherwise every entry becomes
(255, 255, 255) (a failed name lookup also prints `Name To ID failed` with
`OutputDebugStringA`). It builds an OMT 256-entry colour table (0x476090). Callers at
0x434cb0 pick `pal.act`/`dpl.act`/`pnt.act`/`wht.act`/… by horse colour (table 0x802fc8)
and the location palette `aa_west.act`… (table 0x802fa8) by trail location. The
disassembler exports each one as `.act`, a text table and a 16×16 swatch PNG under
`local/extract/scripts/data/` (13/13 decoded).
