# TouchFX

Touch-driven live visuals for Windows, sent out over **NDI** (up to 1920x1080) to Resolume, grandMA3 or any NDI receiver.

Draw on the canvas with one or two fingers. The canvas is the output: where you touch is where the effect appears.

## Install / run

1. Install the free **NDI Tools** (or the NDI Runtime) from <https://ndi.video/tools/>. TouchFX loads NDI from there.
2. Download `TouchFX-windows-x64` (see *Getting a build* below), unzip anywhere, run `TouchFX.exe`.
   It's portable: no installer, no extra DLLs.
3. In Resolume / grandMA3 / NDI Studio Monitor, pick the NDI source named **TouchFX** (the name can be changed in *OUTPUT*).

Requirements: Windows 10/11 64-bit, a GPU with OpenGL 3.3 (any dedicated card), a multi-touch screen (a mouse works too).

## Controls

| Area | What it does |
|---|---|
| **BPM** | Tap **TAP** (or press SPACE) to set the tempo; **-/+** nudge it. `x1/4 … x4` scale effect speed. The 4 lights show the beat. |
| **EFFECT** | Fluid / smoke, Particles, Kaleidoscope, Lines / lasers, Strobe / flash. **BLACKOUT** kills the output. |
| **MIRROR** | Off, left/right, up/down, quad (both), radial 6, radial 8. Guides are drawn on the preview only. |
| **ON RELEASE** | **FADE**: the effect fades out over the chosen time after you lift your fingers. **SNAP**: it goes off instantly. |
| **CLEAR CANVAS** | Wipes the current effect. |
| **EFFECT tab** | Settings for the current effect (size, trail, speed, glow, strobe rate…) and a reset button. |
| **COLOUR tab** | 10 palettes (tap to select). Edit the **LOW / MID / HIGH** colours and name of the selected palette. **Colour mode**: Gradient (over the effect's life), Speed (finger speed), Position (left to right), Beat (steps each beat), Random. Output brightness. |
| **SCENES tab** | 12 slots. **STORE** then tap a slot to save the whole look; tap a slot to recall it. **DELETE** then tap to clear. |
| **OUTPUT tab** | Resolution (1920x1080, 1280x720, 960x540 or custom, max 1920x1080), 30 / 60 fps, NDI on/off and source name, UI size, fullscreen. |

Strobe rate is in flashes per beat (`1/4 … 8`) synced to the BPM, or free-running in Hz. Particles can strobe too (*SYNC*: all together, *SPARKLE*: each particle on its own phase).

Keyboard: `SPACE` tap tempo, `C` clear, `B` blackout, `F11` fullscreen, `Esc` leave fullscreen.

Everything (settings, palettes, scenes) is saved automatically to `%APPDATA%\TouchFX\TouchFX\settings.json`.
Delete that file to go back to factory defaults.

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
- `src/ndi.cpp`: NDI sender (runtime-loaded, runs on its own thread)
- `src/state.cpp`: settings, palettes, scenes and saving

NDI® is a registered trademark of Vizrt NDI AB.
