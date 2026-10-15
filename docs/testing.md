# Testing

## Unit tests

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
OPENBRC_GAME_DATA=/path/to/BRCV1_0.ISO ctest --test-dir build -j8
```

Without `OPENBRC_GAME_DATA` (a disc image, a mounted disc or an installation) the tests that need
the game's files are skipped.

## Scripted runs

The game can run without a window or sound and follow a script:

```sh
XDG_DATA_HOME=$PWD/run/data XDG_CONFIG_HOME=$PWD/run/config \
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
build/bin/openbrc -v --source BRCV1_0.ISO --node stable \
    --autoplay "wait stable; +3000 click 230 220; +2000 snap white; +100 quit"
```

* Keep the run's saves, preferences and snapshots away from your own: point `XDG_DATA_HOME` and
  `XDG_CONFIG_HOME` at a scratch folder. They must be **absolute** paths (relative ones are ignored,
  as the XDG specification says, and the run then uses your real folders). On Windows and macOS
  there is no such override; use a separate user account or back up the data folder.
* Snapshots go to `<data>/openbrc/autoplay/<name>.png` (640×480, the game's own pixels).
* `-v` logs node switches, scripts, sounds and resource loads.

### Command-line options for testing

| option | effect |
|---|---|
| `--node <name>` | start in that scene instead of the logos (factory names: `yard`, `stable`, `rabbitE`, …) |
| `--skip-logos` | start at the preferences screen, as after the logos |
| `--slot <n>` | load saved game `n` before the first scene |
| `--dev-state k=v,…` | set player and session fields: `horse` (0 palomino, 1 white, 2 paint, 3 dapple, 4 none), `saddle`, `firstride`, `finale`, `course`, `control` (0 mouse buttons, 1 keyboard, 2 mouse motion), `mission`, `trail`, `debug`, `coin` (the coin is in the bag, with `m2`), `wellpath`, `gate` (the cottage gate is open), `trailcert` (the trail certificate waits at the next stop or in the yard), mission counters `m1`..`m8`, certificates `c1`..`c4`, badges `b1`..`b3` |

### Autoplay steps

Steps are separated by `;`. `+<ms>` before a step waits that long after the previous one.

| step | effect |
|---|---|
| `wait <node>` | until that scene is current |
| `click <x> <y> [ms]` | move to game pixel (x, y), press the left button on the next pass and release it after `ms` (default 150) |
| `rclick <x> <y> [ms]` | the same with the right button |
| `move <x> <y>` | move the pointer |
| `key <code>` | key press and release with an engine key code (1 Esc, 2 Backspace, 3 Enter, 4 Delete, 6–9 arrows, 10 Tab) |
| `vkey <vk> [char]` | key press with a Windows virtual-key code, the typed character if given, and the release (BarbieRecomp's `key`) |
| `type <text>` | typed characters |
| `snap <name>` | save the frame |
| `quit` | leave at once |

## Comparing with the original

BarbieRecomp (a static recompilation of the 1998 executable) gives reference screenshots for the
same script: `OUT=<dir> ~/Projects/BarbieRecomp/tools/autoplay.sh <seconds> "<script>" --node <name>`.
Its script language is the same apart from `key`, which takes Windows key codes there (use `vkey`
on our side). Its lifter currently treats `fxch st(N)` as a no-op, so floating-point code can
misbehave in the reference (for example movements on the wrong axis); confirm a difference in the
executable before changing our port.

## Pitfalls

* `quit` skips the game's own save. To keep progress for the next run, leave through the exit
  dialog: `vkey 27; +1500 click 215 280` (yes).
* The first intro movie cannot be skipped (until `perf.inf` exists), and the tack room's first
  visit is a tour of about a minute that ignores clicks.
* Scene edges are clicked, not hovered: the right edge of the tack room leads to the yard only with
  a click once the view has scrolled to its end.
* Animated targets move: click the body of the horse rather than its head.
* Until the first ride is done, the yard sends the player on the riding lesson and missions are put
  aside; add `firstride=1` to test missions.
