# Architecture

OpenBRC is split into static-library modules under `src/`, linked into the `openbrc`
executable and the `brctool` developer tool. Lower modules never depend on higher ones.

```
                 app
                  │
                 game ─────────────┐
                  │                │
                engine ──────┐     │
               /   |   \      \    │
         formats  audio render platform
             \     |     /       /
              vfs ─┴── core ────┘
```

| Module | Responsibility |
|---|---|
| core | logging, INI, files, paths, strings, SHA-256, PNG writer |
| vfs | disc images (ISO 9660/Joliet, BIN/CUE), directories, mount stack, game-source detection, the disc manifest |
| formats | the original data: OMT containers (`.omt`, `.omt.cache`), canvases, BLIT sprites, HCAn animations, compiled scripts, WAVE, Smacker |
| platform | SDL3: initialisation, clock, mouse, dialogs |
| render | presentation of the 8-bit 640×480 game screen: Vulkan and OpenGL back ends, scaling, filters, toasts |
| audio | software mixer and SDL3 output |
| engine | the Human Code/OMT runtime the game is written against (below) |
| game | the Barbie Riding Club scenes ("Godiva" nodes), the game-node base, toolbar, pager, saddlebag, player records |
| app | `openbrc`: command line, settings, first-run setup, main loop |

## The engine

The original game is a C++ program on the Open Media Toolkit 1.x plus Human Code's "HC"
classes. OpenBRC keeps that structure so that every scene can be ported function by function
(`docs/engine/*.md` describe the original; class names below are ours):

| OpenBRC | Original | Notes |
|---|---|---|
| `engine::Engine` | `HCApplication` | owns every subsystem below; node switching, fades, quit, the idle-task list, input dispatch |
| `engine::Task` | `OMediaPeriodical` | idle tasks, run once per main-loop pass in creation order |
| `engine::Broadcaster` / `Listener` | OMT broadcaster/listener | numeric messages, delivered in registration order |
| `engine::Resources` | the open-database list | ordered `.omt` files with reference counts; lookup by type and name; images (BLIT before Canv); cache preloading |
| `engine::Image8` | canvases/worlds | 8-bit pixels with a colour key or an opacity mask |
| `engine::Element` | `OMediaElement` | position, depth, hide/pause counters, picking; base of anims, movies and text |
| `engine::Anim`, `AnimRegistry` | `HC2DAnim`, the global anim map | sequences, timing, Smacker or still-image frames, deletion queue |
| `engine::Node` | `HCNode` on `OMediaWorld` | a scene: element list, background, camera, world clock and update modes, picking, the virtual hooks scenes override |
| `engine::ScriptSystem`, `Script` | `HCScript`, the global script map | cooperative bytecode scripts, wake rules, node callbacks |
| `engine::SoundManager` | the HC sound manager over Miles | 16 FIFO channels, one object per sound name, use counts, completion events |
| `engine::Movie` | `HCMovie` and the Godiva Smacker players | Smacker playback as elements |
| `engine::Timer` | Godiva timer | interval timers that broadcast a message |
| `engine::Font`, `TextElement` | `OMediaCanvasFont`, OMT text element | bitmap text |
| `engine::Screen` | the DirectDraw screen | 640×480 8-bit back buffer + palette, presented through `render::Presenter` |

Rules that keep the port faithful:

* **Single-threaded game logic**, scheduled exactly like the original: one main-loop pass runs
  every task once (input, application, timers, the node's world update and render, movies).
  Only presentation (render thread) and audio mixing (SDL audio thread) run elsewhere.
* **Time** comes from one millisecond clock (`Engine::ticks()`, the `GetTickCount` of the
  original). World updates see the elapsed time clamped to 500 ms.
* **Names, not pointers**: anims, scripts, sounds and resources are found by case-insensitive
  name, as in the original, and the global anim/script maps survive node switches.
* **Numbers from the original**: message numbers, click ids, depths, volumes and timings are
  the original values, so ported scene code reads like the original.
* Every frame is redrawn completely (background, then elements by ascending depth); the
  original's dirty rectangles only saved work (display.md §3.1).

## Presentation and modern displays

The engine renders into an 8-bit 640×480 buffer with a 256-colour palette, exactly what the
original drew. `render::Presenter` resolves the palette on the GPU and scales the picture to the
window: 4:3 with bars, stretched, or whole-number scale; sharp, nearest, smooth or CRT filtering;
any refresh rate (the game thread never waits for vsync); HiDPI aware. Vulkan is used when
available, OpenGL (3.3 core, 4.1 on macOS, 2.1 as a last resort) otherwise.

## Game data

`vfs::probeGameSource` identifies a disc image, disc or installation; `vfs::assembleGameData`
mounts them (installation before disc, as the original's HD/CD search order) and checks the
336 data files of the disc manifest. The game never starts without them. Saved games, photos,
printouts and settings live in the user's data directory (`core::paths`).
