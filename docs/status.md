# Status

What works, what is being ported, and what is known to differ from the original. Updated as
scenes land.

## Foundation

| Area | State |
|---|---|
| Game data: disc images, discs, installations; v1.0 manifest check | done |
| OMT containers, canvases, BLIT sprites, HCAn animations, `.omt.cache` | done, validated on every chunk of the disc |
| Compiled scripts (bytecode VM) | done, all 54 scripts run |
| Smacker video and audio, WAVE | done, bit-exact with the reference decoder on all 967 streams |
| Renderer: Vulkan, OpenGL 3.3/4.1/2.1 fallback, scaling and filters, HiDPI, any refresh rate | done |
| Audio mixer (SDL3) | done |
| Engine: anims, nodes, scripts, sound manager, timers, movies, fonts, node switching and fades | done |
| Windows (MSVC, MinGW), Linux, macOS builds and packages | done; macOS untested on hardware |

## Scenes

| Scene | State |
|---|---|
| MattelLogoNode, HCLogoNode, BarbieSoftLogoNode | done |
| Toolbar base (HCToolbarNode), buttons, exit dialog, voices | in progress |
| preferences | in progress |
| signin, intromovie | to do |
| yard, stable, tackroom, grooming, bulletinboard | to do |
| trail rides (appleW, cottageL, bridgeS, rainbowS, rabbitE, beachE, wishingL) and missions | to do |
| applesort, leapfrog, bellgame, vinegame, carousel, race | to do |
| opendoor, finalpayoff, credits | to do |
| Toolbar popups: pager, map, saddlebag, camera and photo album, handbook | to do |

## Deliberate differences

* Printing saves PNG files in the `Printouts` folder instead of using a printer.
* The Desktop Horse (a separate Windows program on the disc) is not installed.
* Saved games, photos and preferences live in the user's data folder; the save files keep the
  original format (`savegame<N>.brc`), so saves from the 1998 game can be copied in.
* The low-end machine test always reports a fast machine (the measurement no longer means
  anything); the letterboxed trail videos are not used.
