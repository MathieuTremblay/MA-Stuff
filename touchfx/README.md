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
Each show is a **project folder**, `Documents\TouchFX Projects\<name>\`, holding the show file (looks, scenes, cue
lists, palettes, DMX patch, media library), its picture and a `media` folder with the show's clips. It saves itself
automatically. Shows from before 0.9.4 (`Documents\TouchFX Shows`) are copied into project folders the first time.
**EXPORT** copies the selected project, media included, to a USB stick or SSD, into a *TouchFX Projects* folder;
**IMPORT** lists the projects on a drive (and older single show files) and brings one in. Big projects copy in the
background with a progress bar and **CANCEL**; nothing is left half-copied. Everything is done on the touch screen
(drives are listed with USB sticks first); if the name is already used you choose **REPLACE** or **KEEP BOTH**.
What belongs to the PC (outputs, screens, audio and video inputs, layout, touch) stays in `settings.json`.
Shows from before 0.8 that used **Lightning** (replaced by **Video**) open with **Plexus** in its place.
*SETTINGS → SYSTEM → SWITCH SHOW* goes back to the launcher.

## Show mode

The header shows the mode at a glance: an amber **EDIT** badge and amber outline around the panel when unlocked,
a green **SHOW** badge and outline in show mode (a blackout's red frame takes over).

**SHOW MODE** (top of the panel) locks everything that could break a show: SETTINGS, scene STORE / DELETE,
saving palettes, cue editing, undo, and the keyboard except SPACE (tap, or GO in the CUES view), ENTER (GO), B
(blackout) and the CUES view keys. Closing TouchFX asks for confirmation. Effects, their sliders, colours, mirror, BPM, scenes and GO keep working.
**Hold the SHOW button for 2 seconds** to unlock. Windows sleep / screen saver are blocked while TouchFX runs.

## Cues

The **CUES** tab is a cue list per song:
- **◀ song ▶** at the top switches song (tap the name for the song list: new, rename, duplicate, reorder, delete,
  renumber, and what GO does after the last cue).
- Cues have **numbers** like a console cue list (1, 2, 2.5, 3...) so they can match your MA cue list. RECORD between
  two cues gives a number in between (2.5); *EDIT → No.* changes a number on a number pad (the list stays sorted);
  *song list → RENUMBER CUES* resets the song to 1, 2, 3...
- Each cue stores the full look (effect, settings, colours, mirror, release, LOOK), a **fade** time and an optional
  **follow** (go on automatically after N seconds or N bars). BPM is never recalled.
- **GO** fires the next cue (orange outline), **BACK** the previous one; tap a cue to make it the next one.
  Different effects crossfade over the cue's fade, the same effect glides every value.
- **RECORD** stores the live look as a new cue after the one on stage, **UPDATE** saves the live look into the cue on stage, or into the cue you picked yourself (tapped or chosen with the arrows) since the last GO; the button shows which (*UPDATE 3*, *UPDATE 4.5 (sel.)*)
  (the cue shows *modified* when you changed something), **EDIT** opens the number, name, fade, follow, move, copy,
  delete.
- **GO on the last cue** (set per song in the song list): **STOP** (end of song), **LOOP TO CUE 1**, or **NEXT SONG**
  (cue 1 of the next song that has cues). A follow on the last cue follows that setting too.
- **UNDO / REDO** (or **Ctrl+Z / Ctrl+Y**) undo cue, song and scene changes (record, update, edit, delete, renumber,
  scene store / delete / rename...). The button shows what it will undo. 50 steps, cleared when the show closes.
- In the CUES view: **E** opens the edit window of the NEXT cue, **Delete** twice deletes it (Ctrl+Z brings it
  back). In the edit window, **Enter** = DONE and **Esc** closes it; taps on it never reach the canvas.
- Keyboard: **Enter** = GO in every tab. In the CUES view: **Space** = GO, **Up / Down** = choose the next cue,
  **Backspace** = BACK, **Left / Right** = previous / next song, **T** = tap tempo. In the other tabs Space stays tap
  tempo. These keys work in show mode too.

## Controls

The panel has three tabs: **LIVE** (everything you touch during the show), **CUES** and **SETTINGS** (set up once).
Swipe up/down anywhere in the panel to scroll (sliders: swipe sideways to change, tap to jump to a value).

### LIVE

| Area | What it does |
|---|---|
| **BPM** | Tap **TAP** (or press SPACE) to set the tempo, or **tap the BPM number** to type it on a keypad; **-/+** nudge it. `x1/4 … x4` scale effect speed. The 4 lights show the beat. |
| **AUDIO** | Turns audio reactive mode on (blue button, with a level bar). See *Audio reactive* below. |
| **Effects** | 16 effects in the bar under the canvas (or in the LIVE tab, see *SCREEN*): Fluid, Particles, Sparkles, Ribbons, Fireworks, Starfield, Plexus, Kaleido, Lasers, **Video**, Tunnel, Shapes, Ripples, Spectrum, LED grid, Strobe. **CLEAR** wipes the canvas (**twice in a row**: the background layer goes out in 0.3 s and turns OFF), **BLACKOUT** kills the output: the preview turns red and striped with a big BLACKOUT label, a red outline goes around the preview and the button pulses (DMX blackout shows the same). |
| **Mirror** | Off, left/right, up/down, quad (both), radial 6, radial 8 (always visible under the tempo). Guides are drawn on the preview only. |
| **On release** | Set **per effect**: **FADE** (the effect keeps going where your fingers were lifted and the image fades out over the chosen time) **SNAP** (off instantly) or **HOLD** (keeps running where your fingers were lifted, reacting to the music, until **CLEAR**). Switching effect crossfades: the old effect fades out (time in *SETTINGS → LOOK*). |
| **Effect settings** | Sliders for the current effect (size, trail, speed, glow, strobe rate…) and a reset button. |
| **New in 0.5** | **Ripples** (water surface, drops on the beat), **Ribbons** (glowing strands following the fingers), **Shapes** (neon polygons / stars popping on the beat), **Tunnel** (full-screen tunnel, vanishing point under the finger), **Plexus** (connected points), **Fireworks** (rockets bursting at the finger), **Starfield** (hyperspace streaks), **Spectrum** (audio equaliser: circle, bars or wave), **LED grid** (LED-wall tiles: trail, rings, sparkle). Strobe gains **Shockwave**, **Wipe**, **Blinder** and **Blocks**. |
| **FRONT / BACK** | Which layer the LIVE controls edit. **FRONT** (orange) is the effect you play with your fingers. **BACK** (blue) is the background layer: the effect bar, sliders, mirror and speed then edit the background (see *Background layer*). |
| **Video** | The video input played with the fingers (choose the input in *SETTINGS → VIDEO IN*): **REVEAL** (paint the video into view, it fades back over the trail time), **LIQUID** (push the picture like a liquid), **RIPPLE** (water ripples bend it), **KALEIDO** (kaleidoscope around your first finger), **PIXEL** (big pixels where your fingers go), **WARP** (swirl / pinch around each finger), **CLEAN** (just the video, nothing added). **ALWAYS ON** keeps the picture up without fingers and it never fades on release (with REVEAL, the unpainted video shows at the *base level*). **FREEZE** (or the **F** key) holds the current frame for both layers, even through a lost signal, until you tap it again (header: *VIDEO FROZEN*; not stored in scenes or cues). The video always goes out exactly like the source: the LOOK (glow, saturation, brightness) only applies to the effects, and with the alpha channel on the video stays opaque. At the default *Video level* (80 %) the picture is 1:1; above it is boosted. Capture cards / cameras: TouchFX uses the colour range and matrix the device reports (full range or video range, BT.601 or BT.709). Fit: fill, fit or stretch. The palette only colours the glow / highlights the effect adds (Glow at 0 = pure video) and the gradient that stands in when there is no input. |
| **Ribbons** | Each finger paints a wide flat stroke of parallel colour bands (**Bands** 2–6, colours from the palette: low, mid, high, repeating), a retro striped look, with rounded ends; turns too tight for the stroke's width are rounded off so the bands never fold. **STAY**: the paint stays where you drew it and ripples gently (**Wave** sets how much, 0 = still; **Flow** how fast). **FLOAT**: the tail also sways and drifts away softly as it fades. It fades after the **Length** time. Matte: the glow is off unless you raise **Glow**. |
| **Spectrum** | **CIRCLE** (bars around the finger), **BARS** (an equaliser across the screen laid out by MIRROR: L-R puts the lows in the centre, U-D grows the bars up and down from the middle), **WAVE**, **WATERFALL** (the spectrum's history flowing away from the finger), **SCOPE** (the sound wave itself), **RINGS** (lows inside, highs outside, each ring pumping), **BURST** (every hit throws out a ring of spikes). |
| **LED grid** | LED-wall tiles: **TRAIL**, **RINGS**, **SPARKLE**, **SCROLL** (colour bands sweeping on the beat, direction from your finger), **EQUALIZER** (VU columns from the audio), **PLASMA**, **RAIN**, **CHECKER** (checker / stripes / quarters flipping on the beat), **RADAR** (sweep around each finger), **VIDEO MAP** (the video input on the tiles), **VU METERS** (big level meters with peak hold), **SPECTRO** (scrolling spectrogram). EQUALIZER, VU METERS and SPECTRO follow MIRROR (L-R: from the centre out, U-D: from the middle row). Square or round tiles, gap, size, and **Bulb**: incandescent lamps (amber when dim, warm white at full, slow to heat up and cool down, a glowing round bulb). |
| **Lasers** | Laser-show modes: **FAN** (fan of beams from the finger), **AIM** (projectors along the bottom converge on your finger), **TUNNEL** (rings on the beat), **WAVE**, **SCAN**, **WEB**. *Swing* sweeps on the beat and *Rotation* turns the beams, both following the **swing pattern** (**TOGETHER**, **MIRROR** each side opposite, **WAVE** one after the other / the fan opens and closes, **RANDOM** each beam its own direction, speed and start, re-picked at every touch); *Scatter* gives each beam a random angle offset; *Chase* makes beams blink in sequence / alternate / random. |
| **Quick colours** | Tap LOW / MID / HIGH under the palettes to override the colours live without touching the palette. **SAVE TO …** stores them in the palette, **BACK TO PALETTE** drops the override. Scenes remember overrides. |
| **Palette / colour mode** | Pick one of the 10 palettes. Colour mode: Gradient (over the effect's life), Speed (finger speed), Position (left to right), Beat (steps each beat), Random, **Video** (picks up the video's colour where your fingers are and the effect carries it: smoke swirls it, particles keep it), **Video mask** (every pixel takes the colour of the video under it). |
| **Scenes** | Under the canvas (or in the LIVE tab). **STORE** then tap a slot to save the whole look and name it; tap a slot to recall (the BPM is never changed by a scene). Long-press a scene to rename it. **DELETE** then tap to clear. While STORE is armed, the button next to it chooses **AS LATCH** (stays on, as before) or **AS FLASH**. |
| **Flash scenes** | Marked with a yellow **FLASH** tag. The scene is on **only while you hold its button**: it snaps in and draws by itself (no finger on the canvas needed), and when you let go everything snaps back exactly as it was: effect, settings, colours, background layer, even what the previous effect had on screen. Hold another FLASH while one is held to switch between blasts. Tapping a normal scene during a flash changes what it returns to; a cue GO ends the flash. From DMX: the flash stays on while the Scene channel stays on it. To rename a FLASH scene, store it again. |

### SETTINGS

| Tab | What it does |
|---|---|
| **COLOURS** | Edit the **LOW / MID / HIGH** colours and name of each palette. |
| **LOOK** | Glow (bloom), glow size, saturation and brightness for every effect (saved in scenes), and the effect-change crossfade time. |
| **AUDIO** | Input level meter (works while the tab is open, even with audio reactive off), audio source (default input, any input device, system audio, test beat), sensitivity, auto BPM, auto play. |
| **OUTPUT** | Resolution (1920x1080, 1280x720, 960x540 or custom, max 1920x1080), 30 / 60 fps, NDI on/off, source name and **network card**, **Spout** output (GPU sharing with Resolume / TouchDesigner / OBS on the same PC), **alpha channel** (straight or premultiplied). |
| **SCREEN** | **External display**: show the output fullscreen on another screen (fit / fill / stretch, or windowed). A screen bigger than the output resolution (e.g. a 4K screen with a 1080p output) gets a sharp bicubic upscale; rendering stays at the output resolution, so NDI and the frame rate are not affected. Layout: panel left or right, panel width, effects / scenes under the canvas, UI size, fullscreen. |
| **DMX** | Control from a lighting console: sACN or Art-Net, universe, start address, Basic / Advanced profile, DMX lock, live channel monitor. |
| **VIDEO IN** | The video input (a PC setting): **NDI**, **CAPTURE** (USB capture devices such as AV Matrix, cameras, cards that appear as a camera), **SPOUT** (another program on this PC), **BLACKMAGIC** (DeckLink / UltraStudio, needs Blackmagic Desktop Video; format detected automatically), **AJA** (KONA / Io / Corvid, needs the AJA driver; each SDI / HDMI input is listed), **TEST** (built-in picture). Pick the source from the list (or *first one found*); status, format, frame rate and a preview are shown. |
| **SYSTEM** | Current show, switch show, show and logs folders; block Windows 3/4-finger gestures, number of touch points (1–10), touch markers on the preview, reset everything. |

### Background layer

A second effect running behind the one you play, on the same output. Select **BACK** at the top of LIVE:
- **ON / OFF** and **Level**, then the effect (the effect bar turns blue), its sliders, its own mirror and speed.
- **MOVED BY**: **AUTO** (it moves by itself: slow wandering points, faster with the music when audio reactive is
  on; **Motion** sets how fast), **TOUCH** (only your fingers move it, like the front effect) or **BOTH**.
- **Blend**: **ADD** (both layers glow together), **BEHIND** (the front effect covers the background where it is
  bright), **REVEAL** (the background only shows where the front effect draws: paint it into view, e.g. a video).
- **Own colours**: its own palette and colour mode, otherwise the front layer's.
- It never fades when you lift your fingers. **CLEAR** clears the front layer only; **BLACKOUT** blacks out
  everything. The header shows what runs behind (*BG: STARFIELD 60%*).
- Scenes and cues store it: a cue fades it in, out or to another effect over the cue's fade time (an effect change
  dips through black).

### Media server (MEDIA window)

**MEDIA** (top of the panel) opens the media server in its **own window**, so TouchFX stays usable: with one touch
screen, both windows can be full screen and **< TOUCHFX** / **MEDIA** switch between them; with a second control
screen, leave the MEDIA window there. It remembers where it was.

- **FILES** (left): browse **PLACES** (your folders and drives, USB sticks included), **UP**, and **search** (typed,
  or **KEYS** for the on-screen keyboard; it searches the folder and the folders inside it). Tick videos / pictures
  and **IMPORT** (or **IMPORT ALL**) into the group shown.
- Every clip is **converted once to HAP Q** (or **HAP Alpha** for transparent clips and pictures) into the
  project's `media` folder, **without audio**, in the background with a progress bar. The graphics card decodes
  HAP, so playback is smooth, starts instantly, plays at any speed and leaves the CPU (and NDI) alone. Files that
  are already HAP are copied as they are. Resolume DXV files can be imported (converted to HAP). Anything bigger
  than 4K comes down to 4K. FFmpeg (included: `ffmpeg.exe` / `ffprobe.exe` next to TouchFX.exe) does the conversion.
- **Groups** (middle, as tabs): **+ GROUP**, **RENAME**, **DELETE GROUP** (tap twice; its clips go too). The first cell
  is always **LIVE INPUT** (the video input from *SETTINGS → VIDEO IN*).
- Like Resolume: **tap a clip's name** to show it in **PREVIEW** (playing, with its settings) and edit it underneath
  without touching the output; **tap its picture** to play it. Tapping the playing clip again restarts it.
  The playing clip has an orange frame and progress bar; colour **labels** (a tag on top of the picture) help to
  find clips. **PLAY IT** plays the previewed clip.
- **PLAYING** (right) shows the media layer; **RESTART**, **STOP**.
- **Settings stored with each clip** (they follow the clip wherever it plays, and can be changed while it plays):
  - **PLAY**: **LOOP**, **ONCE + HOLD** (last frame), **ONCE + BLACK**, **BOUNCE**; **In / Out** trim; **Speed**
    (0–400 %), **REVERSE**; **BEAT SYNC**: the clip (In to Out) lasts 1–64 beats at the current BPM and loops
    on the bar; **transition into this clip** (the layer's, or its own CUT / CROSSFADE / BLACK and time).
  - **TRANSFORM**: **Scale**, **Scale X / Y**, **Position X / Y**, **Rotation**, **FLIP H / V**, **Crop** each side.
  - **COLOUR**: **Gamma**, **Hue**, **Saturation**, **Blur**.
  - **RENAME**, **< / >** (order), **MOVE TO GROUP**, **DELETE CLIP** (tap twice; removes its file from the project).
- **When the clip changes** (the media layer): **CUT**, **CROSSFADE** or **FADE THROUGH BLACK**, with a time.

**Where the media plays:** the media player is a source of the **VIDEO** effect (**VIDEO INPUT** / **MEDIA**). Tapping
a clip's picture puts it on the **BACK layer** (VIDEO, CLEAN, MEDIA, always on, BEHIND) if nothing shows the media
yet, so the effects you play go over it. On the **FRONT**, VIDEO on MEDIA lets REVEAL, LIQUID, RIPPLE, KALEIDO,
PIXEL and WARP work on your clips. The clip goes out exactly like the file (the LOOK does not touch it);
transparent clips stay transparent, also on the alpha output.

**Scenes, cues and FLASH scenes remember the playing clip**: GO changes the clip (with the media transition), and
a FLASH scene can blast a clip; on release the previous clip comes back where it would have been.

### Video input

*SETTINGS → VIDEO IN* chooses one input for the PC; the **VIDEO** effect and the **Video** colour mode use it (on
either layer). The header shows **VIDEO** (green, live), **VIDEO LOST** (yellow) or **NO VIDEO** (red). When the
signal is lost, the last picture is held for 2 seconds, then fades out; TouchFX reconnects by itself when the source
comes back. If the NDI output card is changed, the NDI input reconnects automatically.

### DMX control (grandMA3 and other consoles)

1. In *SETTINGS → DMX* pick **sACN** or **ART-NET**, the **network card** on the lighting network (or AUTOMATIC), the universe (default **sACN universe 100**, Art-Net 99) and the
   start address (default **1**), and the profile mode (**FULL v2** for new shows).
2. Import `gdtf/TouchFX@TouchFX.gdtf` (in the download zip) into grandMA3 and patch **TouchFX** in the same mode at
   the same address.
3. The **DMX** light in the header turns green when data arrives (yellow = signal lost, TouchFX keeps the last look).

Three modes (old patches keep working):
- **Full v2 (87 ch)**, new in 0.9: sub-fixtures **Main**, **Front**, **Back**, **Low / Mid / High** and **Cursor 1-4**.
  - *Main*: master dimmer, strobe, BPM (value = BPM, 0 = TouchFX keeps its tempo), scene, **song** and **cue**
    (cue number x 10, so cue 2.5 = 25; fires that TouchFX cue), cue command (GO / BACK / next / previous / restart
    song), action (clear front, clear front + back, tap), audio reactive, auto play, video freeze, glow, glow size,
    saturation, brightness.
  - *Front* and *Back* have the same channels: effect (all 16), mode, option 1-2, **param 1-10**, speed, mirror,
    palette (or DMX colours), colour mode; Front adds release and fade time, Back adds level (0 = off), blend,
    motion, follow touch, and "same as front" for palette / colour mode.
  - **The encoders take the effect's names**: with Lasers selected, Param 1 is *Beams*, Option 2 *Swing pattern*;
    with Fluid, Param 1 is *Size* (GDTF mode dependencies). Each effect's defaults are TouchFX's defaults.
  - *Cursor 1-4*: virtual fingers (on/off + 16-bit Pan/Tilt). While the console moves them, TouchFX shows them as
    green **C1-C4** crosshairs on the preview (not on the output).
  - Two ways to work: program the look from the console, or fire TouchFX's own cues with Song + Cue.
- **Basic v1 (12 ch)** and **Advanced v1 (36 ch)**: unchanged from 0.5 (first six effects, 2 cursors).

Choice channels use 0-9 = **keep** (no change), then named values. When the Effect channel changes, that effect's
mode, options and params are all applied at once.

With **Art-Net**, TouchFX answers the console's Art-Net polls, so it appears in grandMA3's Art-Net node list; the DMX
tab shows which console has seen it. Channel chart: [`docs/DMX.md`](docs/DMX.md). *Last change wins* between console and panel; **DMX LOCK** makes the
console the only master. No console at hand? `python3 tools/dmx_test_sender.py <TouchFX-PC-IP>` sends test DMX (add `--v2` for a scripted Full v2 sequence).

The header also shows the **clock**, and **CPU / GPU / RAM** usage (GPU = Windows' 3D engine load, like Task Manager).

### Audio reactive

Turn on **AUDIO** and pick a source in *SETTINGS → AUDIO*:
- an input device: a line input from the sound desk (best for shows) or a microphone,
- **System audio**: whatever this PC is playing,
- **Test beat**: a built-in 128 BPM loop to try things without music.

With **AUTO PLAY** on, virtual fingers (blue circles on the preview) draw the current effect by themselves, moved
by the music; they disappear when the music stops, so the effect fades/snaps like a released finger.
Real fingers still work on top. **Reactivity** sets how much the music drives the effect (amount, speed, glow kicks
on hits). Up to **10 auto points**. As soon as a real finger touches, the auto points step aside so the effect
follows your hand at full speed; they come back when you let go.

**FREQUENCY RANGE** (*SETTINGS → AUDIO*) picks the part of the music the hits, the pulse and the motion listen to:
**KICK**, **BASS**, **LOW-MIDS**, **VOCALS**, **HI-HATS**, **FULL**, or your own **From / To** in Hz (moving a slider
switches to CUSTOM).

**AUTO BPM** follows the detected tempo and lines the beat up with the hits. The tempo is measured over the last
8 seconds of the whole groove, so syncopated kicks no longer push it too high. If a song is read at double or half
its tempo (a 68 BPM ballad read as 136), tap **1/2** or **x2** next to *Detected … BPM* once: it stays in that
octave for the song. The strobe can also **flash on audio hits**.

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
Each is a drop-down list (AUTOMATIC or one card).
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

Needs CMake 3.20+ and Visual Studio 2022 (Desktop C++). Dependencies (SDL2, Dear ImGui, nlohmann/json, Spout, the AJA NTV2 SDK) are downloaded by CMake; the Blackmagic DeckLink interface is in `third_party/decklink`.

```bash
cmake -S touchfx -B build -A x64
cmake --build build --config Release
```

Developer self-test (renders every effect with fake fingers and saves screenshots): `TouchFX.exe --selftest <folder>`.

## Stress test

`TouchFX.exe --stress 120` lets a robot play TouchFX for 120 minutes, using this PC's real outputs, video input and
DMX settings: up to 10 fingers, effect / background / scene / cue / undo / song changes, random settings,
resolution changes, the video input dropping out and coming back, DMX floods to this PC (when DMX listens on all
cards), and 400 damaged show files at the start. **Nothing is saved** (shows and settings stay as they were). At the
end it writes `stress-report.txt` (result, frame rate, slowest frame, memory, GPU buffers) and `stress-data.csv`
(measurements every 10 s) to `%APPDATA%\TouchFX\TouchFX` and opens that folder. Run it from a command prompt in the
TouchFX folder, or make a shortcut with `--stress 120` added to the target. To include the media server, set
`TOUCHFX_TEST_MEDIA` to one or more video / picture files separated by `;` (they are converted into a scratch
folder next to the report; the robot then plays and switches clips, changes every clip setting, moves the media
between the BACK layer and the front VIDEO effect, and opens / closes the MEDIA window).

## Code layout

- `src/main.cpp`: window, touch input, mirroring, release/fade logic, NDI readback
- `src/ui.cpp`: the touch control panel
- `src/fx_*.cpp`: one file per effect (GPU shaders)
- `src/post.cpp`: glow (bloom), saturation, tonemapping, alpha, video colours, layer compositing
- `src/video_in.cpp`: video input thread, GPU colour conversion, NDI receive, test source; `src/video_mf.cpp`
  (capture / cameras), `src/video_decklink.cpp` (Blackmagic), `src/video_aja.cpp` (AJA)
- `src/stress.cpp`: the `--stress` robot and report
- `src/audio.cpp`: audio capture (SDL inputs, WASAPI system audio), band levels, hit and tempo detection
- `src/dmx.cpp`, `src/dmx_apply.cpp`: sACN / Art-Net receiver and channel mapping
- `tools/dmx_profile.py`: the DMX profiles; generates `src/dmx_profile.inc`, `gdtf/TouchFX@TouchFX.gdtf`, `docs/DMX.md`
- `src/sysstats.cpp`: CPU / GPU / RAM sampling
- `src/show.cpp`, `src/cues.cpp`, `src/log.cpp`: project folders (export / import / duplicate in the background),
  cue playback, log and crash detection
- `src/hap.cpp`: HAP video (QuickTime index, Snappy, HAP frames); `src/media_player.cpp`: clip playback, settings,
  transitions; `src/media_lib.cpp`, `src/process.cpp`: media import (FFmpeg conversion); `src/ui_media.cpp`: the
  MEDIA window
- `src/ui_launcher.cpp`, `src/ui_cues.cpp`: launcher, on-screen keyboard, show mode, CUES view
- `src/platform.cpp`: Windows touch tweaks (gestures, edge swipes, press-and-hold)
- `res/`: app icon (`make_icon.py` regenerates it) and Windows resources
- `src/ndi.cpp`: NDI sender (runtime-loaded, runs on its own thread)
- `src/state.cpp`: settings, palettes, scenes and saving

NDI® is a registered trademark of Vizrt NDI AB.
