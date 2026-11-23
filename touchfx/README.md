# TouchFX

Touch-driven live visuals for Windows, sent out over **NDI** (up to 1920x1080) to Resolume, grandMA3 or any NDI receiver.

Draw on the canvas with up to 10 fingers. The canvas is the output: where you touch is where the effect appears.

## Install / run

1. Install the free **NDI Tools** (or the NDI Runtime) from <https://ndi.video/tools/>. TouchFX loads NDI from there.
2. Download `TouchFX-windows-x64` (see *Getting a build* below), unzip anywhere, run `TouchFX.exe`.
   It's portable: no installer, no extra DLLs.
3. In Resolume / grandMA3 / NDI Studio Monitor, pick the NDI source named **TouchFX** (the name can be changed in *OUTPUT*).

Requirements: Windows 10/11 64-bit, a GPU with OpenGL 3.3 (any dedicated card), a multi-touch screen (a mouse works too).

## Shows

TouchFX starts on a **launcher**: pick a show and tap **OPEN**, or **NEW SHOW** (named with the on-screen keyboard).
**DUPLICATE**, **RENAME**, **DELETE** (moved to a *Deleted* folder) and **SHOW FOLDER** manage them.
A show holds the looks, scenes, cue lists, palettes and DMX patch, and saves itself automatically to
`Documents\TouchFX Shows\<name>.tfxshow`: copy these files to back up or move shows between PCs.
What belongs to the PC (outputs, screens, audio input, layout, touch) stays in `settings.json`.
*SETTINGS → SYSTEM → SWITCH SHOW* goes back to the launcher.

## Show mode

**SHOW MODE** (top of the panel) locks everything that could break a show: SETTINGS, scene STORE / DELETE,
saving palettes, cue editing, and the keyboard except SPACE (tap), ENTER (GO) and B (blackout). Closing TouchFX asks
for confirmation. Effects, their sliders, colours, mirror, BPM, scenes and GO keep working.
**Hold the SHOW button for 3 seconds** to unlock. Windows sleep / screen saver are blocked while TouchFX runs.

## Cues

The **CUES** tab is a cue list per song:
- **◀ song ▶** at the top switches song (tap the name for the song list: new, rename, duplicate, reorder, delete).
- Each cue stores the full look (effect, settings, colours, mirror, release, LOOK), a **fade** time and an optional
  **follow** (go on automatically after N seconds or N bars). BPM is never recalled.
- **GO** fires the next cue (orange outline), **BACK** the previous one; tap a cue to make it the next one.
  Different effects crossfade over the cue's fade, the same effect glides every value.
- **RECORD** stores the live look as a new cue after the one on stage, **UPDATE** saves live tweaks into it
  (the cue shows *modified* when you changed something), **EDIT** opens the name, fade, follow, move, copy, delete.
- GO is also in the LIVE tab (under the mirror) and on the **Enter** key.

## Controls

The panel has three tabs: **LIVE** (everything you touch during the show), **CUES** and **SETTINGS** (set up once).
Swipe up/down anywhere in the panel to scroll (sliders: swipe sideways to change, tap to jump to a value).

### LIVE

| Area | What it does |
|---|---|
| **BPM** | Tap **TAP** (or press SPACE) to set the tempo, or **tap the BPM number** to type it on a keypad; **-/+** nudge it. `x1/4 … x4` scale effect speed. The 4 lights show the beat. |
| **AUDIO** | Turns audio reactive mode on (blue button, with a level bar). See *Audio reactive* below. |
| **Effects** | 16 effects in the bar under the canvas (or in the LIVE tab, see *SCREEN*): Fluid, Particles, Sparkles, Ribbons, Fireworks, Starfield, Plexus, Kaleido, Lasers, Lightning, Tunnel, Shapes, Ripples, Spectrum, LED grid, Strobe. **CLEAR** wipes the canvas, **BLACKOUT** kills the output. |
| **Mirror** | Off, left/right, up/down, quad (both), radial 6, radial 8 (always visible under the tempo). Guides are drawn on the preview only. |
| **On release** | Set **per effect**: **FADE** (fades out over the chosen time after you lift your fingers) or **SNAP** (off instantly). Switching effect crossfades: the old effect fades out (time in *SETTINGS → LOOK*). |
| **Effect settings** | Sliders for the current effect (size, trail, speed, glow, strobe rate…) and a reset button. |
| **New in 0.5** | **Ripples** (water surface, drops on the beat), **Ribbons** (glowing strands following the fingers), **Shapes** (neon polygons / stars popping on the beat), **Tunnel** (full-screen tunnel, vanishing point under the finger), **Lightning** (arcs between fingers), **Plexus** (connected points), **Fireworks** (rockets bursting at the finger), **Starfield** (hyperspace streaks), **Spectrum** (audio equaliser: circle, bars or wave), **LED grid** (LED-wall tiles: trail, rings, sparkle). Strobe gains **Shockwave**, **Wipe**, **Blinder** and **Blocks**. |
| **Lasers** | Laser-show modes: **FAN** (fan of beams from the finger), **AIM** (projectors along the bottom converge on your finger), **TUNNEL** (rings on the beat), **WAVE**, **SCAN**, **WEB**. *Swing* sweeps on the beat, *Chase* makes beams blink in sequence / alternate / random. |
| **Quick colours** | Tap LOW / MID / HIGH under the palettes to override the colours live without touching the palette. **SAVE TO …** stores them in the palette, **BACK TO PALETTE** drops the override. Scenes remember overrides. |
| **Palette / colour mode** | Pick one of the 10 palettes. Colour mode: Gradient (over the effect's life), Speed (finger speed), Position (left to right), Beat (steps each beat), Random. |
| **Scenes** | Under the canvas (or in the LIVE tab). **STORE** then tap a slot to save the whole look and name it; tap a slot to recall (the BPM is never changed by a scene). Long-press a scene to rename it. **DELETE** then tap to clear. |

### SETTINGS

| Tab | What it does |
|---|---|
| **COLOURS** | Edit the **LOW / MID / HIGH** colours and name of each palette. |
| **LOOK** | Glow (bloom), glow size, saturation and brightness for every effect (saved in scenes), and the effect-change crossfade time. |
| **AUDIO** | Input level meter (works while the tab is open, even with audio reactive off), audio source (default input, any input device, system audio, test beat), sensitivity, auto BPM, auto play. |
| **OUTPUT** | Resolution (1920x1080, 1280x720, 960x540 or custom, max 1920x1080), 30 / 60 fps, NDI on/off, source name and **network card**, **Spout** output (GPU sharing with Resolume / TouchDesigner / OBS on the same PC), **alpha channel** (straight or premultiplied). |
| **SCREEN** | **External display**: show the output fullscreen on another screen (fit / fill / stretch, or windowed). Layout: panel left or right, panel width, effects / scenes under the canvas, UI size, fullscreen. |
| **DMX** | Control from a lighting console: sACN or Art-Net, universe, start address, Basic / Advanced profile, DMX lock, live channel monitor. |
| **SYSTEM** | Current show, switch show, show and logs folders; block Windows 3/4-finger gestures, number of touch points (1–10), touch markers on the preview, reset everything. |

### DMX control (grandMA3 and other consoles)

1. In *SETTINGS → DMX* pick **sACN** or **ART-NET**, the **network card** on the lighting network (or AUTOMATIC), the universe (default **sACN universe 100**, Art-Net 99) and the
   start address (default **1**), and the profile mode.
   The DMX effect channel controls the first six effects (Fluid, Particles, Kaleido, Lasers, Strobe, Sparkles);
   the newer ones are reached through scenes until the profile is extended.
2. Import `gdtf/TouchFX@TouchFX.gdtf` (in the download zip) into grandMA3 and patch **TouchFX** in the same mode at
   the same address.
3. The **DMX** light in the header turns green when data arrives (yellow = signal lost, TouchFX keeps the last look).

Two modes:
- **Basic (12 ch)**: dimmer, effect, scene, palette, mirror, BPM, speed, shutter / strobe, size / amount, glow, control.
- **Advanced (36 ch)**: everything, with **instances**: *Low*, *Mid*, *High* colours (RGB, so colour presets, pickers
  and colour phasers work) and *Cursor 1 / Cursor 2* (dimmer + 16-bit Pan/Tilt = X/Y on the canvas, so position
  presets and position effects draw on the canvas; a cursor draws while its dimmer is up).

With **Art-Net**, TouchFX answers the console's Art-Net polls, so it appears in grandMA3's Art-Net node list; the DMX
tab shows which console has seen it. Channel chart: [`docs/DMX.md`](docs/DMX.md). *Last change wins* between console and panel; **DMX LOCK** makes the
console the only master. No console at hand? `python3 tools/dmx_test_sender.py <TouchFX-PC-IP>` sends test DMX.

The header also shows the **clock**, and **CPU / GPU / RAM** usage (GPU = Windows' 3D engine load, like Task Manager).

### Audio reactive

Turn on **AUDIO** and pick a source in *SETTINGS → AUDIO*:
- an input device: a line input from the sound desk (best for shows) or a microphone,
- **System audio**: whatever this PC is playing,
- **Test beat**: a built-in 128 BPM loop to try things without music.

With **AUTO PLAY** on, virtual fingers (blue circles on the preview) draw the current effect by themselves, moved
by the music; they disappear when the music stops, so the effect fades/snaps like a released finger.
Real fingers still work on top. **Reactivity** sets how much the music drives the effect (amount, speed, glow kicks
on hits). **AUTO BPM** follows the detected tempo and lines the beat up with the hits. The strobe can also
**flash on audio hits**.

### Windows touch gestures

Windows uses 3- and 4-finger swipes for itself (switch apps, show desktop), which gets in the way when drawing with
several fingers. TouchFX turns those off while it runs and puts your setting back when you quit
(*SETTINGS → SYSTEM → BLOCK 3/4-FINGER GESTURES*). Edge swipes are blocked while TouchFX is fullscreen (F11).
On some Windows builds the gesture change only applies after signing out; if swipes still work, turn off
*Settings → Bluetooth & devices → Touch → Three- and four-finger touch gestures* once on the show PC
(the *OPEN WINDOWS TOUCH SETTINGS* button goes straight there).

Keyboard: `SPACE` tap tempo, `C` clear, `B` blackout, `F11` or `Alt+Enter` fullscreen, `Esc` leave fullscreen. The **FULL** button next to the clock does the same by touch.

Everything (settings, palettes, scenes) is saved automatically to `%APPDATA%\TouchFX\TouchFX\settings.json`.
Delete that file to go back to factory defaults.

## Network cards

With several network cards (lighting network, video network, internet), choose which one is used:
- **NDI** (*SETTINGS → OUTPUT*): the feed is announced and sent only on that card. TouchFX writes its own copy of
  NDI's configuration (keeping your NDI Access Manager settings) with only that adapter allowed; AUTOMATIC leaves
  NDI's configuration untouched. NDI restarts for a moment when you change it.
- **DMX** (*SETTINGS → DMX*): sACN joins its multicast group only on that card, only packets from that network are
  accepted, and the Art-Net poll reply gives the console that card's address.

These choices belong to the PC (not the show). If the chosen card disappears (cable out, adapter disabled), it stays
selected in red, the NDI / DMX lights show **NO NETWORK**, and TouchFX waits for it instead of using another network.

## Logs and crashes

TouchFX writes `touchfx.log` (and keeps the previous run as `touchfx-previous.log`) in `%APPDATA%\TouchFX\TouchFX`.
If it ever closes unexpectedly, the launcher says so on the next start and a crash dump (`crash-*.dmp`) is saved in
the same folder: *SETTINGS → SYSTEM → LOGS FOLDER* opens it.

## Getting a build

Every push to `touchfx/` builds on GitHub Actions. Open the repo's **Actions** tab, pick the latest *TouchFX Windows build* run and download the **TouchFX-windows-x64** artifact.
Pushing a tag like `touchfx-v0.1.0` also publishes a zip on the **Releases** page.

## Building it yourself

Needs CMake 3.20+ and Visual Studio 2022 (Desktop C++). Dependencies (SDL2, Dear ImGui, nlohmann/json) are downloaded by CMake.

```bash
cmake -S touchfx -B build -A x64
cmake --build build --config Release
```

Developer self-test (renders every effect with fake fingers and saves screenshots): `TouchFX.exe --selftest <folder>`.

## Code layout

- `src/main.cpp`: window, touch input, mirroring, release/fade logic, NDI readback
- `src/ui.cpp`: the touch control panel
- `src/fx_*.cpp`: one file per effect (GPU shaders)
- `src/post.cpp`: glow (bloom), saturation, tonemapping, alpha
- `src/audio.cpp`: audio capture (SDL inputs, WASAPI system audio), band levels, hit and tempo detection
- `src/dmx.cpp`, `src/dmx_apply.cpp`: sACN / Art-Net receiver and channel mapping
- `tools/dmx_profile.py`: the DMX profiles; generates `src/dmx_profile.inc`, `gdtf/TouchFX@TouchFX.gdtf`, `docs/DMX.md`
- `src/sysstats.cpp`: CPU / GPU / RAM sampling
- `src/show.cpp`, `src/cues.cpp`, `src/log.cpp`: show files, cue playback, log and crash detection
- `src/ui_launcher.cpp`, `src/ui_cues.cpp`: launcher, on-screen keyboard, show mode, CUES view
- `src/platform.cpp`: Windows touch tweaks (gestures, edge swipes, press-and-hold)
- `res/`: app icon (`make_icon.py` regenerates it) and Windows resources
- `src/ndi.cpp`: NDI sender (runtime-loaded, runs on its own thread)
- `src/state.cpp`: settings, palettes, scenes and saving

NDI® is a registered trademark of Vizrt NDI AB.
