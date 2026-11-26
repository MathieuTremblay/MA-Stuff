#!/usr/bin/env python3
"""Single source of truth for the TouchFX DMX profiles.

Generates:
  src/dmx_profile.inc        C++ channel tables used by the app
  gdtf/TouchFX@TouchFX.gdtf  GDTF fixture file (import into grandMA3)
  docs/DMX.md                channel chart

Run from the touchfx folder:  python3 tools/dmx_profile.py
"""
import io
import os
import uuid
import zipfile
import xml.etree.ElementTree as ET
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIXTURE_ID = "6E1B4C2A-7F3D-4B8E-9A51-2C0D7E9F4A10"  # keep stable across versions
VERSION = "2.0"

# ---------------------------------------------------------------------------
# Discrete channels share one layout: 0-9 = "keep" (no change), then equal slots from 10 to 249,
# 250-255 = special (keep, or DMX colours for the palette channel in Advanced).
SLOT_START = 10
SLOT_END = 249  # inclusive


def slot_size(count):
    return (SLOT_END - SLOT_START + 1) // count


EFFECTS = ["Fluid", "Particles", "Kaleido", "Lasers", "Strobe", "Sparkles"]  # app enum order
MIRRORS = ["Off", "Left-Right", "Up-Down", "Quad", "Radial 6", "Radial 8"]
SPEEDS = ["x1/4", "x1/2", "x1", "x2", "x4"]
PALETTES = [f"Palette {i}" for i in range(1, 11)]
SCENES = [f"Scene {i}" for i in range(1, 13)]
MODES = [f"Mode {i}" for i in range(1, 9)]
CONTROL = [  # (from, name)
    (0, "Nothing"), (10, "Clear canvas"), (30, "Blackout (hold)"), (50, "Audio on"), (70, "Audio off"),
    (90, "Auto play on"), (110, "Auto play off"), (130, "Release: fade"), (150, "Release: snap"),
    (170, "Nothing 2"),
]
EFFECT_PARAMS = {  # Adjust1..4 per effect (documentation; the app implements the same mapping)
    "Fluid": ["Size", "Force", "Swirl", "Trail"],
    "Particles": ["Amount", "Size", "Speed", "Trail"],
    "Sparkles": ["Amount", "Size", "Twinkle", "Spread"],
    "Kaleido": ["Brush", "Rotation", "Zoom", "Trail"],
    "Lasers": ["Spread", "Rotation", "Swing", "Width"],
    "Strobe": ["Flash length", "Size", "Rate (1/4..8 per beat)", "-"],
}
MODE_NAMES = {
    "Lasers": ["Fan", "Aim", "Web", "Scan", "Tunnel", "Wave"],
    "Strobe": ["Full", "Spot", "H-bar", "V-bar", "Sparkle"],
    "Particles": ["Strobe off", "Strobe sync", "Strobe sparkle"],
}

# function id, geometry, GDTF attribute, bytes, pretty name, kind, extra
#   kinds: "linear", "slots", "bpm", "shutter", "control", "pan", "tilt"
F = {}


def fn(fid, attr, nbytes, pretty, kind, **kw):
    F[fid] = dict(id=fid, attr=attr, bytes=nbytes, pretty=pretty, kind=kind, **kw)


fn("DIMMER", "Dimmer", 1, "Dimmer", "linear", default=0)
fn("EFFECT", "Effects1", 1, "Effect", "slots", items=EFFECTS, default=0)
fn("FXMODE", "Effects2", 1, "Effect mode", "slots", items=MODES, default=0)
fn("SCENE", "MediaContent1", 1, "Scene", "slots", items=SCENES, default=0)
fn("PALETTE", "ColorMacro1", 1, "Palette", "slots", items=PALETTES, default=0)
fn("PALETTE_ADV", "ColorMacro1", 1, "Palette", "slots", items=PALETTES, default=250, special="DMX colours (instances)")
fn("MIRROR", "Effects3", 1, "Mirror", "slots", items=MIRRORS, default=0)
fn("BPM", "Effects1Rate", 2, "BPM", "bpm", default=0)
fn("SPEED", "PlaySpeed", 1, "Speed", "slots", items=SPEEDS, default=0)
fn("SHUTTER", "Shutter1", 1, "Shutter", "shutter", default=0)
fn("PARAM1", "Effects1Adjust1", 1, "Param 1", "linear", default=128)
fn("PARAM2", "Effects1Adjust2", 1, "Param 2", "linear", default=128)
fn("PARAM3", "Effects1Adjust3", 1, "Param 3", "linear", default=128)
fn("PARAM4", "Effects1Adjust4", 1, "Param 4", "linear", default=128)
fn("GLOW", "Effects1Adjust5", 1, "Glow", "linear", default=140)
fn("SATURATION", "VideoSaturation", 1, "Saturation", "linear", default=158)
fn("CONTROL", "Control1", 1, "Control", "control", default=0)
for c, (r, g, b) in (("LOW", (0, 30, 140)), ("MID", (30, 120, 255)), ("HIGH", (160, 230, 255))):
    fn(f"{c}_R", "ColorAdd_R", 1, "Red", "linear", default=r)
    fn(f"{c}_G", "ColorAdd_G", 1, "Green", "linear", default=g)
    fn(f"{c}_B", "ColorAdd_B", 1, "Blue", "linear", default=b)
for k in (1, 2):
    fn(f"CUR{k}_DIM", "Dimmer", 1, "Dimmer", "linear", default=0)
    fn(f"CUR{k}_X", "Pan", 2, "Pan (X)", "pan", default=32768)
    fn(f"CUR{k}_Y", "Tilt", 2, "Tilt (Y)", "tilt", default=32768)


# ===========================================================================================================
# Profile v2 ("Full v2"): sub-fixtures Master / Front / Back / Low / Mid / High / Cursor 1-4.
# Mode, Option 1-2 and Param 1-10 of each layer take their names (and defaults) from the Effect channel
# (GDTF ModeMaster), so grandMA3 shows the real setting on every encoder.
# ===========================================================================================================
RATES = ["1/4 per beat", "1/2 per beat", "1 per beat", "2 per beat", "4 per beat", "8 per beat"]
CMODES = ["Gradient", "Speed", "Position", "Beat", "Random", "Video", "Video mask"]


def P(name, field, default, lo=None, hi=None, note=""):
    """A param: 0-255 -> 0..1 (lo/hi None), or an integer / float range."""
    return dict(name=name, field=field, default=default, lo=lo, hi=hi, note=note)


def CH(name, field, names, values=None, default=0, special=None):
    """A named choice (mode / option): slot i sets field = values[i] (default: i)."""
    return dict(name=name, field=field, names=names, values=values if values is not None else list(range(len(names))),
                default=default, special=special)


ONOFF = ["Off", "On"]
# Effects in the order of the effect buttons (= order on the Effect channel). C++ enum, display name, ...
FX2 = [
    ("FX_FLUID", "Fluid", None, None, None,
     [P("Size", "fluid.size", .4), P("Force", "fluid.force", .5), P("Swirl", "fluid.swirl", .4),
      P("Trail", "fluid.trail", .6), P("Intensity", "fluid.intensity", .5)]),
    ("FX_PARTICLES", "Particles", CH("Strobe", "particles.strobe", ["Strobe off", "Strobe sync", "Strobe sparkle"]),
     CH("Strobe rate", "particles.strobeRate", RATES, default=3), None,
     [P("Amount", "particles.amount", .6), P("Size", "particles.size", .5), P("Life", "particles.life", .5),
      P("Speed", "particles.speed", .4), P("Spread", "particles.spread", .6),
      P("Gravity", "particles.gravity", .5, note="50% = none"), P("Trail", "particles.trail", .3)]),
    ("FX_SPARKLES", "Sparkles", None, None, None,
     [P("Amount", "sparkles.amount", .5), P("Size", "sparkles.size", .5), P("Spread", "sparkles.spread", .35),
      P("Twinkle", "sparkles.twinkle", .5), P("Life", "sparkles.life", .5), P("Drift", "sparkles.drift", .25),
      P("Rays", "sparkles.rays", .6), P("Trail", "sparkles.trail", .1)]),
    ("FX_RIBBONS", "Ribbons", CH("Ribbon motion", "ribbons.motion", ["Stay", "Float"]), None, None,
     [P("Bands", "ribbons.bands", 4, 2, 6), P("Length", "ribbons.length", .5), P("Width", "ribbons.width", .5),
      P("Wave", "ribbons.wave", .4), P("Flow", "ribbons.flow", .4), P("Glow", "ribbons.glow", 0)]),
    ("FX_FIREWORKS", "Fireworks", None, CH("Launch rate", "fireworks.rate", RATES, default=2),
     CH("Rockets", "fireworks.rockets", ONOFF, default=1),
     [P("Size", "fireworks.size", .5), P("Amount", "fireworks.amount", .5), P("Gravity", "fireworks.gravity", .5),
      P("Trail", "fireworks.trail", .5), P("Sparkle", "fireworks.sparkle", .5)]),
    ("FX_STARFIELD", "Starfield", None, None, None,
     [P("Amount", "starfield.amount", .3), P("Speed", "starfield.speed", .4), P("Streak", "starfield.streak", .35),
      P("Size", "starfield.size", .3), P("Twinkle", "starfield.twinkle", .3),
      P("Brightness", "starfield.brightness", .5)]),
    ("FX_PLEXUS", "Plexus", None, None, None,
     [P("Points", "plexus.amount", .5), P("Link distance", "plexus.range", .5), P("Dot size", "plexus.size", .4),
      P("Drift", "plexus.drift", .4), P("Spread", "plexus.spread", .5), P("Trail", "plexus.trail", .1)]),
    ("FX_KALEIDO", "Kaleido", None, None, None,
     [P("Segments", "kaleido.segments", 8, 2, 16), P("Rotation", "kaleido.rotation", .6, note="50% = stop"),
      P("Zoom", "kaleido.zoom", .6, note="50% = none"), P("Warp", "kaleido.warp", .4),
      P("Brush", "kaleido.brush", .4), P("Trail", "kaleido.trail", .6)]),
    ("FX_LASERS", "Lasers", CH("Laser mode", "lasers.mode", ["Fan", "Aim", "Web", "Scan", "Tunnel", "Wave"]),
     CH("Chase", "lasers.chase", ["Off", "Step", "Alternate", "Random"]),
     CH("Swing pattern", "lasers.swingMode", ["Together", "Mirror", "Wave", "Random"]),
     [P("Beams", "lasers.beams", 8, 1, 32), P("Rotation", "lasers.rotation", .6, note="50% = stop"),
      P("Spread", "lasers.spread", .45), P("Swing", "lasers.swing", .3), P("Scatter", "lasers.scatter", 0),
      P("Width", "lasers.width", .25), P("Glow", "lasers.glow", .5), P("Length", "lasers.length", 1),
      P("Trail", "lasers.trail", .15)]),
    ("FX_VIDEO", "Video", CH("Video mode", "video.mode", ["Reveal", "Liquid", "Ripple", "Kaleido", "Pixel", "Warp", "Clean"]),
     CH("Fit", "video.fit", ["Fill", "Fit", "Stretch"]), CH("Always on", "video.alwaysOn", ONOFF),
     [P("Size", "video.size", .5), P("Strength", "video.strength", .5), P("Trail", "video.trail", .6),
      P("Video level", "video.level", .8), P("Glow", "video.glow", .3), P("Base level", "video.base", .3),
      P("Segments", "video.segments", 6, 2, 16)]),
    ("FX_TUNNEL", "Tunnel", CH("Sides", "tunnel.sides", ["Round", "3 sides", "4 sides", "5 sides", "6 sides", "7 sides",
                                                          "8 sides"], values=[0, 3, 4, 5, 6, 7, 8]),
     CH("Merge fingers", "tunnel.merge", ONOFF), None,
     [P("Speed", "tunnel.speed", .5), P("Ring density", "tunnel.density", .5),
      P("Spin", "tunnel.twist", .5, note="50% = stop"), P("Line width", "tunnel.width", .35),
      P("Edge fade", "tunnel.depth", .5)]),
    ("FX_GEOMETRY", "Shapes", CH("Shape", "geometry.shape", ["Triangle", "Square", "Pentagon", "Hexagon", "Circle", "Star",
                                                             "Mix"], default=6),
     CH("Rate", "geometry.rate", RATES, default=3), None,
     [P("Size", "geometry.size", .5), P("Spin", "geometry.spin", .65, note="50% = stop"),
      P("Line width", "geometry.width", .3), P("Life", "geometry.life", .5), P("Glow", "geometry.glow", .5)]),
    ("FX_RIPPLES", "Ripples", None, CH("Beat drops", "ripples.beatDrops", ONOFF, default=1), None,
     [P("Size", "ripples.size", .4), P("Strength", "ripples.strength", .55), P("Decay", "ripples.decay", .5),
      P("Shine", "ripples.shine", .6)]),
    ("FX_SPECTRUM", "Spectrum", CH("Spectrum mode", "spectrum.mode", ["Circle", "Bars", "Wave", "Waterfall", "Scope", "Rings", "Burst"]),
     CH("Symmetric", "spectrum.symmetric", ONOFF, default=1), None,
     [P("Bars", "spectrum.bars", 48, 8, 128), P("Size", "spectrum.size", .5), P("Gain", "spectrum.gain", .5),
      P("Width", "spectrum.width", .4), P("Glow", "spectrum.glow", .5)]),
    ("FX_GRID", "LED grid", CH("Grid mode", "grid.mode", ["Trail", "Rings", "Sparkle", "Scroll", "Equalizer", "Plasma",
                                                          "Rain", "Checker", "Radar", "Video map", "VU meters", "Spectro"]),
     CH("Tile shape", "grid.round", ["Square", "Round"], values=[0.0, 1.0]), None,
     [P("Tile size", "grid.tile", .4), P("Gap", "grid.gap", .3), P("Bulb", "grid.bulb", 0),
      P("Brush", "grid.radius", .4, note="pattern size in whole-wall modes"), P("Trail", "grid.decay", .5)]),
    ("FX_STROBE", "Strobe", CH("Strobe shape", "strobe.shape", ["Full", "Spot", "H-bar", "V-bar", "Sparkle", "Shockwave",
                                                                "Wipe", "Blinder", "Blocks"]),
     CH("Flash rate", "strobe.rate", RATES + ["Free (Param 3)", "Audio hits"], default=3, special="strobe"),
     CH("Colour cycle", "strobe.colorCycle", ONOFF, default=1),
     [P("Flash length", "strobe.duty", .3), P("Size", "strobe.size", .4),
      P("Free rate", "strobe.hz", 8.0, 1.0, 25.0, note="Hz, with Flash rate = Free")]),
]
NPARAM2 = 10
assert all(len(f[5]) <= NPARAM2 for f in FX2)

SPEEDS2 = ["x1/4", "x1/2", "x1", "x2", "x4"]
MIRRORS2 = ["Off", "Left-Right", "Up-Down", "Quad", "Radial 6", "Radial 8"]
PALETTES2 = [f"Palette {i}" for i in range(1, 11)]


def slot_ranges(n):
    """(from, to) of each slot over 10-249 (the last one runs to 249)."""
    size = 240 // n
    return [(10 + i * size, 249 if i == n - 1 else 10 + (i + 1) * size - 1) for i in range(n)]


# v2 channels: id, geometry, GDTF attribute, bytes, name, kind, extra
#   kinds: dim (0-255 %), choice (named slots), trig (trigger ranges of 40), value (raw number shown),
#          cue (16-bit cue x10), shutter, pan, tilt, level (0 = off), modefx / optfx / paramfx (named by effect)
V2CH = []


def c2(cid, geo, attr, nbytes, name, kind, **kw):
    V2CH.append(dict(id=cid, geo=geo, attr=attr, bytes=nbytes, name=name, kind=kind, **kw))


TRIG_CUE = ["GO", "BACK", "Next song", "Previous song", "Restart song"]
TRIG_ACT = ["Clear front", "Clear front + back", "Tap tempo"]
c2("M_DIMMER", "Body", "Dimmer", 1, "Master dimmer", "dim", default=255)
c2("M_SHUTTER", "Body", "Shutter1", 1, "Strobe", "shutter", default=0)
c2("M_BPM", "Body", "Effects1Rate", 1, "BPM", "value", lo=20, hi=255, zero="TouchFX tempo", default=0,
   note="0-19 = TouchFX keeps its own tempo; 20-255 = BPM")
c2("M_SCENE", "Body", "MediaContent1", 1, "Scene", "value", lo=1, hi=12, zero="Keep", default=0)
c2("M_SONG", "Body", "MediaFolder1", 1, "Song", "value", lo=1, hi=255, zero="Keep", default=0)
c2("M_CUE", "Body", "MediaContent2", 2, "Cue", "cue", default=0)
c2("M_CUECMD", "Body", "Control1", 1, "Cue command", "trig", items=TRIG_CUE, default=0)
c2("M_ACTION", "Body", "Control2", 1, "Action", "trig", items=TRIG_ACT, default=0)
c2("M_AUDIO", "Body", "Control3", 1, "Audio reactive", "choice", items=ONOFF, default=0)
c2("M_AUTOPLAY", "Body", "Control4", 1, "Auto play", "choice", items=ONOFF, default=0)
c2("M_FREEZE", "Body", "Control5", 1, "Video freeze", "choice", items=["Live", "Frozen"], default=0)
c2("M_GLOW", "Body", "Effects1Adjust1", 1, "Glow", "dim", default=140)
c2("M_GLOWSIZE", "Body", "Effects1Adjust2", 1, "Glow size", "dim", default=153)
c2("M_SAT", "Body", "VideoSaturation", 1, "Saturation", "dim", default=158)
c2("M_BRIGHT", "Body", "VideoBrightness", 1, "Brightness", "dim", default=128)


def layer(pfx, geo, back):
    if back:
        c2(f"{pfx}_LEVEL", geo, "Dimmer", 1, "Level", "level", default=0)
    c2(f"{pfx}_EFFECT", geo, "Effects1", 1, "Effect", "choice", items=[f[1] for f in FX2], default=0)
    c2(f"{pfx}_MODE", geo, "Effects2", 1, "Mode", "modefx", default=0)
    c2(f"{pfx}_OPT1", geo, "Effects3", 1, "Option 1", "optfx", which=1, default=0)
    c2(f"{pfx}_OPT2", geo, "Effects4", 1, "Option 2", "optfx", which=2, default=0)
    if back:
        c2(f"{pfx}_BLEND", geo, "Effects5", 1, "Blend", "choice", items=["Add", "Behind", "Reveal"], default=0)
        c2(f"{pfx}_MOTION", geo, "Effects2Rate", 1, "Motion", "dim", default=102)
        c2(f"{pfx}_FOLLOW", geo, "Effects6", 1, "Motion source", "choice", items=["Auto", "Touch", "Both"], default=0)
    else:
        c2(f"{pfx}_RELEASE", geo, "Effects5", 1, "Release", "choice", items=["Fade", "Snap", "Hold"], default=0)
        c2(f"{pfx}_FADETIME", geo, "Effects1Fade", 1, "Fade time", "fadetime", default=38)
    c2(f"{pfx}_SPEED", geo, "Effects1Rate", 1, "Speed", "choice", items=SPEEDS2, default=0)
    c2(f"{pfx}_MIRROR", geo, "Effects7", 1, "Mirror", "choice", items=MIRRORS2, default=0)
    pal = (["Same as front"] if back else []) + PALETTES2
    c2(f"{pfx}_PALETTE", geo, "ColorMacro1", 1, "Palette", "choice", items=pal, special="DMX colours", default=0)
    cm = (["Same as front"] if back else []) + CMODES
    c2(f"{pfx}_CMODE", geo, "ColorMacro2", 1, "Colour mode", "choice", items=cm, default=0)
    for i in range(1, NPARAM2 + 1):
        c2(f"{pfx}_P{i}", geo, f"Effects1Adjust{i}", 1, f"Param {i}", "paramfx", index=i - 1, default=0)


layer("F", "Front", False)
layer("B", "Back", True)
for col, rgb in (("Low", (0, 30, 140)), ("Mid", (30, 120, 255)), ("High", (160, 230, 255))):
    for comp, v in zip(("R", "G", "B"), rgb):
        c2(f"{col.upper()}_{comp}", col, f"ColorAdd_{comp}", 1, {"R": "Red", "G": "Green", "B": "Blue"}[comp], "dim",
           default=v)
for k in range(1, 5):
    c2(f"C{k}_DIM", f"Cursor{k}", "Dimmer", 1, "On / off", "cursor", default=0)
    c2(f"C{k}_X", f"Cursor{k}", "Pan", 2, "Pan (X)", "pan", default=32768)
    c2(f"C{k}_Y", f"Cursor{k}", "Tilt", 2, "Tilt (Y)", "tilt", default=32768)

V2_ATTRS = {  # attribute: (feature group, feature, pretty, unit)
    "Effects1Rate": ("Beam", "Beam", "Speed", "None"), "MediaContent1": ("Gobo", "Media", "Scene", "None"),
    "MediaFolder1": ("Gobo", "Media", "Song", "None"), "MediaContent2": ("Gobo", "Media", "Cue", "None"),
    "Control2": ("Control", "Control", "Action", "None"), "Control3": ("Control", "Control", "Audio", "None"),
    "Control4": ("Control", "Control", "Auto play", "None"), "Control5": ("Control", "Control", "Freeze", "None"),
    "VideoBrightness": ("Color", "HSBC_Shift", "Brightness", "Percent"),
    "Effects4": ("Beam", "Beam", "Option 2", "None"), "Effects5": ("Beam", "Beam", "Release / Blend", "None"),
    "Effects6": ("Beam", "Beam", "Follow", "None"), "Effects7": ("Beam", "Beam", "Mirror", "None"),
    "Effects2Rate": ("Beam", "Beam", "Motion", "None"), "Effects1Fade": ("Beam", "Beam", "Fade time", "Time"),
    "ColorMacro2": ("Color", "RGB", "Colour mode", "None"),
}
for i in range(1, NPARAM2 + 1):
    V2_ATTRS.setdefault(f"Effects1Adjust{i}", ("Beam", "Beam", f"Param {i}", "None"))


def layout2():
    out, off = [], 0
    for c in V2CH:
        out.append((c, off))
        off += c["bytes"]
    return out, off


def param_dmx_default(p):
    if p["lo"] is None:
        return int(round(p["default"] * 255))
    return int(round((p["default"] - p["lo"]) / (p["hi"] - p["lo"]) * 255))


def choice_dmx_default(ch):
    i = ch["values"].index(ch["values"][ch["default"]])
    a, b = slot_ranges(len(ch["names"]))[i]
    return (a + b) // 2


# Modes: list of (function id, geometry)
BASIC = [("DIMMER", "Body"), ("EFFECT", "Body"), ("SCENE", "Body"), ("PALETTE", "Body"), ("MIRROR", "Body"),
         ("BPM", "Body"), ("SPEED", "Body"), ("SHUTTER", "Body"), ("PARAM1", "Body"), ("GLOW", "Body"),
         ("CONTROL", "Body")]
ADVANCED = [("DIMMER", "Body"), ("EFFECT", "Body"), ("FXMODE", "Body"), ("SCENE", "Body"), ("PALETTE_ADV", "Body"),
            ("MIRROR", "Body"), ("BPM", "Body"), ("SPEED", "Body"), ("SHUTTER", "Body"), ("PARAM1", "Body"),
            ("PARAM2", "Body"), ("PARAM3", "Body"), ("PARAM4", "Body"), ("GLOW", "Body"), ("SATURATION", "Body"),
            ("CONTROL", "Body")]
ADVANCED += [(f"{c}_{x}", c.capitalize()) for c in ("LOW", "MID", "HIGH") for x in "RGB"]
ADVANCED += [(f"CUR{k}_{x}", f"Cursor{k}") for k in (1, 2) for x in ("DIM", "X", "Y")]
MODES_DEF = [("Basic", BASIC, "Palette-based control, 12 channels"),
             ("Advanced", ADVANCED, "Full control with colour and cursor instances, 36 channels")]


def layout(mode):
    out, off = [], 0
    for fid, geo in mode:
        out.append((fid, geo, off))
        off += F[fid]["bytes"]
    return out, off


# ---------------------------------------------------------------------------
def write_inc():
    ids = list(F.keys())
    lines = ["// Generated by tools/dmx_profile.py - do not edit by hand.", "#pragma once", "",
             "enum DmxFn {"]
    lines += [f"  DMX_{i}," for i in ids] + ["  DMX_FN_COUNT", "};", ""]
    lines += ["struct DmxChannelDef {", "  DmxFn fn;", "  int offset;  // 0-based from the start address",
              "  int bytes;   // 1 or 2 (16-bit, coarse first)", "};", ""]
    lines += [f"constexpr int kDmxSlotStart = {SLOT_START};", f"constexpr int kDmxSlotEnd = {SLOT_END};", ""]
    for name, mode, _ in MODES_DEF:
        lay, foot = layout(mode)
        lines.append(f"static const DmxChannelDef kDmx{name}[] = {{")
        for fid, geo, off in lay:
            lines.append(f"    {{DMX_{fid}, {off}, {F[fid]['bytes']}}},  // {off + 1}: {geo} {F[fid]['pretty']}")
        lines.append("};")
        lines.append(f"constexpr int kDmx{name}Footprint = {foot};")
        lines.append("")
    lines += write_inc_v2()
    with open(os.path.join(ROOT, "src", "dmx_profile.inc"), "w") as f:
        f.write("\n".join(lines))



def write_inc_v2():
    L = ["", "// ---- Full v2 -------------------------------------------------------------------------------------",
         "enum DmxFn2 {"]
    L += [f"  D2_{c['id']}," for c in V2CH] + ["  D2_COUNT", "};", "",
          "struct DmxChannelDef2 {", "  DmxFn2 fn;", "  int offset;", "  int bytes;", "};", "",
          "static const DmxChannelDef2 kDmxFull[] = {"]
    for c, off in layout2()[0]:
        L.append(f"    {{D2_{c['id']}, {off}, {c['bytes']}}},  // {off + 1}: {c['geo']} {c['name']}")
    L += ["};", f"constexpr int kDmxFullFootprint = {layout2()[1]};", "",
          f"constexpr int kDmx2FxCount = {len(FX2)};",
          "// effects in the order of the Effect channel",
          "static const int kDmx2Effects[kDmx2FxCount] = {" + ", ".join(f[0] for f in FX2) + "};", "",
          "// 0-9 and 250-255 = keep (-1), otherwise the slot (the last slot runs to 249)",
          "inline int dmx2Slot(int v, int n) {",
          "  if (n <= 0 || v < 10 || v > 249) return -1;",
          "  int s = (v - 10) / (240 / n);",
          "  return s < n ? s : n - 1;",
          "}", ""]

    def choice_body(ch, enum):
        field = ch["field"]
        if ch.get("special") == "strobe":
            return ["      if (slot < 6) { l.strobe.rate = slot; l.strobe.freeHz = false; l.strobe.audioHits = false; }",
                    "      else if (slot == 6) { l.strobe.freeHz = true; l.strobe.audioHits = false; }",
                    "      else { l.strobe.audioHits = true; }"]
        vals = ", ".join(repr(float(v)) + "f" if isinstance(v, float) else str(v) for v in ch["values"])
        typ = "float" if isinstance(ch["values"][0], float) else "int"
        return [f"      static const {typ} v[] = {{{vals}}};",
                f"      if (slot < {len(ch['names'])}) l.{field} = decltype(l.{field})(v[slot]);"]

    def counts(which):
        out = []
        for f in FX2:
            ch = f[2] if which == 0 else (f[3] if which == 1 else f[4])
            out.append(str(len(ch["names"])) if ch else "0")
        return out

    L += ["// number of named choices of Mode (0), Option 1 (1), Option 2 (2) for an effect",
          "inline int dmx2ChoiceCount(int fx, int which) {",
          "  static const int n[3][kDmx2FxCount] = {",
          "      {" + ", ".join(counts(0)) + "},", "      {" + ", ".join(counts(1)) + "},",
          "      {" + ", ".join(counts(2)) + "}};",
          "  for (int i = 0; i < kDmx2FxCount; ++i)",
          "    if (kDmx2Effects[i] == fx) return n[which][i];",
          "  return 0;", "}", "",
          "// sets Mode (which 0) / Option 1 (1) / Option 2 (2) of effect fx to slot",
          "inline void dmx2SetChoice(Look& l, int fx, int which, int slot) {",
          "  if (slot < 0) return;"]
    for which in range(3):
        L.append(f"  if (which == {which}) switch (fx) {{")
        for f in FX2:
            ch = f[2] if which == 0 else (f[3] if which == 1 else f[4])
            if not ch:
                continue
            L.append(f"    case {f[0]}: {{")
            L += choice_body(ch, f[0])
            L.append("      break;")
            L.append("    }")
        L.append("    default: break;")
        L.append("  }")
    L += ["}", "",
          "// Param 1-10 (index 0-9) of effect fx from a DMX value 0-255",
          "inline void dmx2SetParam(Look& l, int fx, int index, int v) {",
          "  float t = v / 255.f;",
          "  switch (fx) {"]
    for f in FX2:
        L.append(f"    case {f[0]}:")
        L.append("      switch (index) {")
        for i, pr in enumerate(f[5]):
            fld = pr["field"]
            if pr["lo"] is None:
                L.append(f"        case {i}: l.{fld} = t; break;")
            elif isinstance(pr["lo"], float) or isinstance(pr["default"], float):
                L.append(f"        case {i}: l.{fld} = {float(pr['lo'])}f + t * {float(pr['hi'] - pr['lo'])}f; break;")
            else:
                L.append(f"        case {i}: l.{fld} = {pr['lo']} + int(t * {pr['hi'] - pr['lo']} + 0.5f); break;")
        L.append("        default: break;")
        L.append("      }")
        L.append("      break;")
    L += ["    default: break;", "  }", "}", ""]
    return L


# ---------------------------------------------------------------------------
FEATURES = {
    "Dimmer": ("Dimmer", "Dimmer"), "Pan": ("Position", "PanTilt"), "Tilt": ("Position", "PanTilt"),
    "ColorAdd_R": ("Color", "RGB"), "ColorAdd_G": ("Color", "RGB"), "ColorAdd_B": ("Color", "RGB"),
    "ColorMacro1": ("Color", "RGB"), "VideoSaturation": ("Color", "HSBC_Shift"),
    "Shutter1": ("Beam", "Beam"), "Shutter1Strobe": ("Beam", "Beam"),
    "Effects1": ("Beam", "Beam"), "Effects2": ("Beam", "Beam"), "Effects3": ("Beam", "Beam"),
    "Effects1Rate": ("Beam", "Beam"), "Effects1Adjust1": ("Beam", "Beam"), "Effects1Adjust2": ("Beam", "Beam"),
    "Effects1Adjust3": ("Beam", "Beam"), "Effects1Adjust4": ("Beam", "Beam"), "Effects1Adjust5": ("Beam", "Beam"),
    "MediaContent1": ("Gobo", "Media"), "PlaySpeed": ("Gobo", "Media"), "Control1": ("Control", "Control"),
}
ATTR_INFO = {  # pretty, physical unit, activation group, main attribute, CIE colour
    "Dimmer": ("Dim", None, None, None, None),
    "Pan": ("P", "Angle", "PanTilt", None, None),
    "Tilt": ("T", "Angle", "PanTilt", None, None),
    "ColorAdd_R": ("R", "ColorComponent", "ColorRGB", None, "0.7347,0.2653,100"),
    "ColorAdd_G": ("G", "ColorComponent", "ColorRGB", None, "0.1596,0.8404,100"),
    "ColorAdd_B": ("B", "ColorComponent", "ColorRGB", None, "0.0366,0.0001,100"),
    "ColorMacro1": ("Color Macro1", None, None, None, None),
    "VideoSaturation": ("Saturation", "Percent", None, None, None),
    "Shutter1": ("Sh1", None, None, None, None),
    "Shutter1Strobe": ("Strobe1", "Frequency", None, "Shutter1", None),
    "Effects1": ("Effect", None, None, None, None),
    "Effects2": ("Effect Mode", None, None, None, None),
    "Effects3": ("Mirror", None, None, None, None),
    "Effects1Rate": ("BPM", "Frequency", None, None, None),
    "Effects1Adjust1": ("Param 1", None, None, None, None),
    "Effects1Adjust2": ("Param 2", None, None, None, None),
    "Effects1Adjust3": ("Param 3", None, None, None, None),
    "Effects1Adjust4": ("Param 4", None, None, None, None),
    "Effects1Adjust5": ("Glow", None, None, None, None),
    "MediaContent1": ("Scene", None, None, None, None),
    "PlaySpeed": ("Speed", "Percent", None, None, None),
    "Control1": ("Control", None, None, None, None),
}


def dmx(v, nbytes):
    return f"{v}/{nbytes}"


def sub(parent, tag, **attrs):
    return ET.SubElement(parent, tag, {k: str(v) for k, v in attrs.items() if v is not None})


def channel_functions(lc, f):
    """Fills a LogicalChannel with ChannelFunctions/ChannelSets for function f."""
    a, nb, kind = f["attr"], f["bytes"], f["kind"]
    maxv = 255 if nb == 1 else 65535
    default = dmx(f.get("default", 0), nb)
    if kind == "linear":
        cf = sub(lc, "ChannelFunction", Name=f["pretty"], Attribute=a, DMXFrom=dmx(0, nb), Default=default,
                 PhysicalFrom=0, PhysicalTo=1)
    elif kind in ("pan", "tilt"):
        sub(lc, "ChannelFunction", Name=f["pretty"], Attribute=a, DMXFrom=dmx(0, nb), Default=default,
            PhysicalFrom=-90, PhysicalTo=90)
    elif kind == "bpm":
        cf = sub(lc, "ChannelFunction", Name="Local tempo", Attribute=a, DMXFrom=dmx(0, nb), Default=default,
                 PhysicalFrom=0, PhysicalTo=0)
        sub(lc, "ChannelFunction", Name="BPM 20-300", Attribute=a, DMXFrom=dmx(256, nb), Default=dmx(256, nb),
            PhysicalFrom=round(20 / 60, 4), PhysicalTo=5)
    elif kind == "shutter":
        sub(lc, "ChannelFunction", Name="Open", Attribute="Shutter1", DMXFrom=dmx(0, nb), Default=default,
            PhysicalFrom=0, PhysicalTo=0)
        sub(lc, "ChannelFunction", Name="Strobe", Attribute="Shutter1Strobe", DMXFrom=dmx(32, nb),
            Default=dmx(32, nb), PhysicalFrom=1, PhysicalTo=25)
        sub(lc, "ChannelFunction", Name="Open 2", Attribute="Shutter1", DMXFrom=dmx(224, nb), Default=dmx(224, nb),
            PhysicalFrom=0, PhysicalTo=0)
    elif kind == "control":
        cf = sub(lc, "ChannelFunction", Name="Control", Attribute=a, DMXFrom=dmx(0, nb), Default=default)
        for frm, name in CONTROL:
            sub(cf, "ChannelSet", Name=name, DMXFrom=dmx(frm, nb))
    elif kind == "slots":
        cf = sub(lc, "ChannelFunction", Name=f["pretty"], Attribute=a, DMXFrom=dmx(0, nb), Default=default)
        sub(cf, "ChannelSet", Name="Keep", DMXFrom=dmx(0, nb))
        size = slot_size(len(f["items"]))
        for i, item in enumerate(f["items"]):
            sub(cf, "ChannelSet", Name=item, DMXFrom=dmx(SLOT_START + i * size, nb))
        last = SLOT_START + len(f["items"]) * size
        if last < 250:
            sub(cf, "ChannelSet", Name="Keep 2", DMXFrom=dmx(last, nb))
        sub(cf, "ChannelSet", Name=f.get("special", "Keep 2"), DMXFrom=dmx(250, nb))
    del maxv


def v2_functions(lc, c):
    """Channel functions of a v2 channel (mode-dependent ones get one function per effect)."""
    nb = c["bytes"]
    geo = c["geo"]
    master = f"{geo}_Effects1"  # GDTF names a DMX channel <geometry>_<attribute>
    k = c["kind"]
    d = lambda v: dmx(v, nb)
    if k in ("dim", "level", "cursor"):
        sub(lc, "ChannelFunction", Name=c["name"], Attribute=c["attr"], DMXFrom=d(0), Default=d(c["default"]),
            PhysicalFrom=0, PhysicalTo=1)
    elif k == "fadetime":
        sub(lc, "ChannelFunction", Name="Fade time", Attribute=c["attr"], DMXFrom=d(0), Default=d(c["default"]),
            PhysicalFrom=0, PhysicalTo=10)
    elif k in ("pan", "tilt"):
        sub(lc, "ChannelFunction", Name=c["name"], Attribute=c["attr"], DMXFrom=d(0), Default=d(c["default"]),
            PhysicalFrom=-90, PhysicalTo=90)
    elif k == "shutter":
        sub(lc, "ChannelFunction", Name="Open", Attribute="Shutter1", DMXFrom=d(0), Default=d(0), PhysicalFrom=0,
            PhysicalTo=0)
        sub(lc, "ChannelFunction", Name="Strobe", Attribute="Shutter1Strobe", DMXFrom=d(32), Default=d(32),
            PhysicalFrom=1, PhysicalTo=25)
        sub(lc, "ChannelFunction", Name="Open 2", Attribute="Shutter1", DMXFrom=d(224), Default=d(224),
            PhysicalFrom=0, PhysicalTo=0)
    elif k == "value":
        sub(lc, "ChannelFunction", Name=c["zero"], Attribute=c["attr"], DMXFrom=d(0), Default=d(0), PhysicalFrom=0,
            PhysicalTo=0)
        f = sub(lc, "ChannelFunction", Name=c["name"], Attribute=c["attr"], DMXFrom=d(c["lo"]), Default=d(c["lo"]),
                PhysicalFrom=c["lo"], PhysicalTo=255)
        if c["id"] in ("M_SCENE", "M_SONG"):
            for v in range(c["lo"], min(c["hi"], 64) + 1):
                sub(f, "ChannelSet", Name=f"{c['name']} {v}", DMXFrom=d(v))
            if c["hi"] < 255:
                sub(f, "ChannelSet", Name="Keep", DMXFrom=d(c["hi"] + 1))
    elif k == "cue":
        sub(lc, "ChannelFunction", Name="Keep", Attribute=c["attr"], DMXFrom=d(0), Default=d(0), PhysicalFrom=0,
            PhysicalTo=0)
        sub(lc, "ChannelFunction", Name="Cue", Attribute=c["attr"], DMXFrom=d(1), Default=d(10), PhysicalFrom=0.1,
            PhysicalTo=6553.5)
    elif k == "trig":
        f = sub(lc, "ChannelFunction", Name=c["name"], Attribute=c["attr"], DMXFrom=d(0), Default=d(0))
        sub(f, "ChannelSet", Name="Idle", DMXFrom=d(0))
        for i, it in enumerate(c["items"]):
            sub(f, "ChannelSet", Name=it, DMXFrom=d(10 + i * 40))
        sub(f, "ChannelSet", Name="Idle 2", DMXFrom=d(10 + len(c["items"]) * 40))
    elif k == "choice":
        f = sub(lc, "ChannelFunction", Name=c["name"], Attribute=c["attr"], DMXFrom=d(0), Default=d(c["default"]))
        sub(f, "ChannelSet", Name="Keep", DMXFrom=d(0))
        for (a, _), it in zip(slot_ranges(len(c["items"])), c["items"]):
            sub(f, "ChannelSet", Name=it, DMXFrom=d(a))
        sub(f, "ChannelSet", Name=c.get("special") or "Keep 2", DMXFrom=d(250))
    elif k in ("modefx", "optfx", "paramfx"):
        ranges = slot_ranges(len(FX2))
        generic = c["name"]
        # Effect channel at keep (0-9 / 250-255): a plain function
        for n, (a, b) in ((generic, (0, 9)), (generic + " 2", (250, 255))):
            sub(lc, "ChannelFunction", Name=n, Attribute=c["attr"], DMXFrom=d(0), Default=d(0), PhysicalFrom=0,
                PhysicalTo=1, ModeMaster=master, ModeFrom=dmx(a, 1), ModeTo=dmx(b, 1))
        for (enum, fxname, mode, o1, o2, params), (a, b) in zip(FX2, ranges):
            mm = dict(ModeMaster=master, ModeFrom=dmx(a, 1), ModeTo=dmx(b, 1))
            if k == "paramfx":
                i = c["index"]
                if i < len(params):
                    pr = params[i]
                    lo = 0 if pr["lo"] is None else pr["lo"]
                    hi = 1 if pr["hi"] is None else pr["hi"]
                    sub(lc, "ChannelFunction", Name=f"{fxname} {pr['name']}", Attribute=c["attr"], DMXFrom=d(0),
                        Default=d(param_dmx_default(pr)), PhysicalFrom=lo, PhysicalTo=hi, **mm)
                else:
                    sub(lc, "ChannelFunction", Name=f"{fxname} unused {i + 1}", Attribute=c["attr"], DMXFrom=d(0),
                        Default=d(0), PhysicalFrom=0, PhysicalTo=1, **mm)
            else:
                ch = mode if k == "modefx" else (o1 if c["which"] == 1 else o2)
                if ch is None:
                    sub(lc, "ChannelFunction", Name=f"{fxname} no {c['name'].lower()}", Attribute=c["attr"],
                        DMXFrom=d(0), Default=d(0), PhysicalFrom=0, PhysicalTo=1, **mm)
                    continue
                f = sub(lc, "ChannelFunction", Name=f"{fxname} {ch['name']}", Attribute=c["attr"], DMXFrom=d(0),
                        Default=d(choice_dmx_default(ch)), **mm)
                sub(f, "ChannelSet", Name="Keep", DMXFrom=d(0))
                for (sa, _), it in zip(slot_ranges(len(ch["names"])), ch["names"]):
                    sub(f, "ChannelSet", Name=it, DMXFrom=d(sa))
                sub(f, "ChannelSet", Name="Keep 2", DMXFrom=d(250))


def write_v2_mode(modes):
    m = sub(modes, "DMXMode", Name="Full v2",
            Description="Both layers, cues, video and 4 cursors with named encoders, "
                        f"{layout2()[1]} channels", Geometry="Body")
    chans = sub(m, "DMXChannels")
    for c, off in layout2()[0]:
        offs = ",".join(str(off + 1 + b) for b in range(c["bytes"]))
        ch = sub(chans, "DMXChannel", DMXBreak=1, Offset=offs, Highlight="None", Geometry=c["geo"])
        snap = c["kind"] in ("choice", "trig", "modefx", "optfx", "value", "cue")
        lc = sub(ch, "LogicalChannel", Attribute=c["attr"], Snap="Yes" if snap else "No",
                 Master="Grand" if c["id"] == "M_DIMMER" else "None", MibFade=0, DMXChangeTimeLimit=0)
        v2_functions(lc, c)
    sub(m, "Relations")
    sub(m, "FTMacros")


def write_gdtf():
    root = ET.Element("GDTF", DataVersion="1.1")
    ft = sub(root, "FixtureType", Name="TouchFX", ShortName="TouchFX", LongName="TouchFX touch visuals",
             Manufacturer="TouchFX", Description="Touch-driven visuals with NDI output. Modes: Basic, Advanced (v1) and Full v2.",
             FixtureTypeID=FIXTURE_ID, Thumbnail="thumbnail", CanHaveChildren="No")
    ad = sub(ft, "AttributeDefinitions")
    ag = sub(ad, "ActivationGroups")
    for g in ("PanTilt", "ColorRGB"):
        sub(ag, "ActivationGroup", Name=g)
    fg = sub(ad, "FeatureGroups")
    groups = {}
    for a, (g, feat) in FEATURES.items():
        groups.setdefault(g, [])
        if feat not in groups[g]:
            groups[g].append(feat)
    for a, (g, feat, _, _) in V2_ATTRS.items():
        groups.setdefault(g, [])
        if feat not in groups[g]:
            groups[g].append(feat)
    for g, feats in groups.items():
        e = sub(fg, "FeatureGroup", Name=g, Pretty=g)
        for feat in feats:
            sub(e, "Feature", Name=feat)
    attrs = sub(ad, "Attributes")
    for a, (pretty, unit, act, main, col) in ATTR_INFO.items():
        g, feat = FEATURES[a]
        sub(attrs, "Attribute", Name=a, Pretty=pretty, ActivationGroup=act, Feature=f"{g}.{feat}",
            MainAttribute=main, PhysicalUnit=unit or "None", Color=col)
    for a, (g, feat, pretty, unit) in V2_ATTRS.items():
        if a in ATTR_INFO:
            continue
        sub(attrs, "Attribute", Name=a, Pretty=pretty, Feature=f"{g}.{feat}", PhysicalUnit=unit)
    sub(ft, "Wheels")
    sub(ft, "PhysicalDescriptions")
    sub(ft, "Models")
    geos = sub(ft, "Geometries")
    ident = "{1,0,0,0}{0,1,0,0}{0,0,1,0}{0,0,0,1}"
    body = sub(geos, "Geometry", Name="Body", Position=ident)
    for i, child in enumerate(["Front", "Back", "Low", "Mid", "High", "Cursor1", "Cursor2", "Cursor3", "Cursor4"]):
        sub(body, "Geometry", Name=child, Position=ident)
    modes = sub(ft, "DMXModes")
    for name, mode, desc in MODES_DEF:
        m = sub(modes, "DMXMode", Name=name, Description=desc, Geometry="Body")
        chans = sub(m, "DMXChannels")
        lay, _ = layout(mode)
        for fid, geo, off in lay:
            f = F[fid]
            offs = ",".join(str(off + 1 + b) for b in range(f["bytes"]))
            ch = sub(chans, "DMXChannel", DMXBreak=1, Offset=offs, Highlight="None", Geometry=geo)
            lc = sub(ch, "LogicalChannel", Attribute=f["attr"], Snap="Yes" if f["kind"] in ("slots", "control") else "No",
                     Master="Grand" if f["attr"] == "Dimmer" and geo == "Body" else "None", MibFade=0,
                     DMXChangeTimeLimit=0)
            channel_functions(lc, f)
        sub(m, "Relations")
        sub(m, "FTMacros")
    write_v2_mode(modes)
    rev = sub(ft, "Revisions")
    sub(rev, "Revision", Text=f"TouchFX profile {VERSION}", Date=datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S"),
        UserID=0, ModifiedBy="TouchFX")
    sub(ft, "FTPresets")
    sub(ft, "Protocols")
    ET.indent(root, "  ")
    xml = '<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n' + ET.tostring(root, encoding="unicode") + "\n"

    os.makedirs(os.path.join(ROOT, "gdtf"), exist_ok=True)
    path = os.path.join(ROOT, "gdtf", "TouchFX@TouchFX.gdtf")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("description.xml", xml)
        thumb = os.path.join(ROOT, "res", "touchfx.png")
        if os.path.exists(thumb):
            z.write(thumb, "thumbnail.png")
    return xml



def v2_values_text(c):
    k = c["kind"]
    if k in ("dim", "cursor"):
        return "0-255 = 0-100 %" if k == "dim" else "0-2 off, 3-255 on"
    if k == "level":
        return "0 = background off, 1-255 = on, level 0-100 %"
    if k == "fadetime":
        return "0-255 = 0-10 s"
    if k in ("pan", "tilt"):
        return "16-bit, " + ("left to right" if k == "pan" else "bottom to top")
    if k == "shutter":
        return "0-31 open, 32-223 strobe 1-25 Hz, 224-255 open"
    if k == "value":
        z = "0" if c["lo"] == 1 else f"0-{c['lo'] - 1}"
        return f"{z} = {c['zero']}, {c['lo']}-{c['hi']} = {c['name'].lower()} (value = number)" + (
            ", above = keep" if c["hi"] < 255 else "")
    if k == "cue":
        return "16-bit, 0 = keep, otherwise cue number x 10 (cue 2.5 = 25); fires when the value changes"
    if k == "trig":
        parts = [f"{10 + i * 40}-{49 + i * 40} {it}" for i, it in enumerate(c["items"])]
        return "0-9 idle, " + ", ".join(parts) + f", {10 + len(c['items']) * 40}-255 idle (fires on entering a range)"
    if k == "choice":
        parts = [f"{a}-{b} {it}" for (a, b), it in zip(slot_ranges(len(c["items"])), c["items"])]
        return "0-9 keep, " + ", ".join(parts) + f", 250-255 {c.get('special') or 'keep'}"
    if k == "modefx":
        return "named per effect (see below)"
    if k == "optfx":
        return "named per effect (see below)"
    if k == "paramfx":
        return "0-255, named per effect (see below)"
    return ""


def doc_v2():
    lay, foot = layout2()
    L = [f"## Full v2 ({foot} channels)", "",
         "Sub-fixtures: **Main** (master), **Front** (the effect you play), **Back** (background layer), **Low / Mid / "
         "High** (colours, used when a Palette is set to *DMX colours*), **Cursor 1-4** (virtual fingers). Front and Back "
         "share the same channels. Mode, Option 1-2 and Param 1-10 take the names of the selected effect in grandMA3.",
         "", "| Ch | Sub-fixture | Function | Values |", "|---|---|---|---|"]
    for c, off in lay:
        ch = f"{off + 1}" + (f"-{off + 2}" if c["bytes"] == 2 else "")
        geo = {"Body": "Main"}.get(c["geo"], c["geo"].replace("Cursor", "Cursor "))
        L.append(f"| {ch} | {geo} | {c['name']} | {v2_values_text(c)} |")
    L += ["", "### Effects: Mode, Option 1, Option 2 and Params (both layers)", "",
          "Choices use 0-9 keep, then equal slots from 10 to 249 in the order listed. Params are 0-100 % unless a "
          "range is given.", "",
          "| Effect (Effect channel) | Mode | Option 1 | Option 2 | " + " | ".join(f"P{i}" for i in range(1, NPARAM2 + 1)) + " |",
          "|---" * (4 + NPARAM2) + "|"]
    for (enum, name, mode, o1, o2, params), (a, b) in zip(FX2, slot_ranges(len(FX2))):
        def chs(ch):
            return f"{ch['name']}: " + ", ".join(ch["names"]) if ch else "-"
        ps = []
        for i in range(NPARAM2):
            if i < len(params):
                pr = params[i]
                rng = f" ({pr['lo']:g}-{pr['hi']:g})" if pr["lo"] is not None else ""
                ps.append(pr["name"] + rng + (f", {pr['note']}" if pr["note"] else ""))
            else:
                ps.append("-")
        L.append(f"| {name} ({a}-{b}) | {chs(mode)} | {chs(o1)} | {chs(o2)} | " + " | ".join(ps) + " |")
    L += ["", "Two ways to work: the console programs the look with these channels, or it fires TouchFX's own cues "
          "with **Song** + **Cue** (cue number x 10). *Last change wins* applies as in v1; with **DMX LOCK** the console "
          "always wins. When the Effect channel changes, the effect keeps its own TouchFX settings; a Mode / Option / "
          "Param only takes over when its DMX value changes.", ""]
    return L


# ---------------------------------------------------------------------------
def write_doc():
    L = ["# TouchFX DMX profiles", "",
         "Generated by `tools/dmx_profile.py`. The GDTF file `gdtf/TouchFX@TouchFX.gdtf` contains all three modes (Basic, Advanced, Full v2).", "",
         "Default patch: **sACN universe 100, address 1** (Art-Net: universe 99, since Art-Net counts from 0).", "",
         "Discrete channels (effect, scene, palette, mirror, speed, mode) use the same layout: "
         f"**0-9 = keep** (no change, the panel stays in charge), then equal slots from {SLOT_START} to {SLOT_END}, "
         "**250-255 = keep** (on the Advanced palette channel: **use the DMX colours** from the Low/Mid/High instances).",
         "", "TouchFX follows *last change wins*: a channel only takes over when its DMX value changes, so the touch "
         "panel and the console can both be used. With **DMX LOCK** on, DMX always wins.", "",
         "**FLASH scenes** (stored as FLASH on the panel) stay on while the Scene channel stays on them; moving the "
         "channel away (to Keep / 0 or another scene) snaps everything back to what was running before.", ""]
    for name, mode, desc in MODES_DEF:
        lay, foot = layout(mode)
        L += [f"## {name} ({foot} channels)", "", desc, "", "| Ch | Instance | Function | Values |", "|---|---|---|---|"]
        for fid, geo, off in lay:
            f = F[fid]
            ch = f"{off + 1}" + (f"-{off + 2}" if f["bytes"] == 2 else "")
            k = f["kind"]
            if k == "slots":
                size = slot_size(len(f["items"]))
                vals = ", ".join(f"{SLOT_START + i * size}-{SLOT_START + (i + 1) * size - 1} {it}"
                                 for i, it in enumerate(f["items"]))
                vals = "0-9 keep, " + vals + (f", 250-255 {f['special']}" if f.get("special") else "")
            elif k == "bpm":
                vals = "16-bit. 0-255 = local tempo (tap / audio), 256-65535 = 20-300 BPM"
            elif k == "shutter":
                vals = "0-31 open, 32-223 strobe 1-25 Hz, 224-255 open"
            elif k == "control":
                vals = ", ".join(f"{frm}" + (f"-{CONTROL[i + 1][0] - 1}" if i + 1 < len(CONTROL) else "-255") + f" {n}"
                                 for i, (frm, n) in enumerate(CONTROL))
            elif k in ("pan", "tilt"):
                vals = "16-bit, " + ("left to right" if k == "pan" else "bottom to top") + " (centre = 32768). Cursor draws while its dimmer is up."
            elif fid.startswith("PARAM"):
                vals = "0-255, depends on the effect (see below)"
            else:
                vals = "0-255"
            inst = "Main" if geo == "Body" else geo
            L.append(f"| {ch} | {inst} | {f['pretty']} ({f['attr']}) | {vals} |")
        L.append("")
    L += ["## Effect parameters (Param 1-4)", "", "| Effect | Param 1 | Param 2 | Param 3 | Param 4 |", "|---|---|---|---|---|"]
    for e in EFFECTS:
        L.append(f"| {e} | " + " | ".join(EFFECT_PARAMS[e]) + " |")
    L += ["", "Basic mode's *Param 1* is the effect's Param 1 (size / amount).", "",
          "## Effect mode (Advanced)", "", "| Effect | Mode 1 | Mode 2 | Mode 3 | Mode 4 | Mode 5 | Mode 6 |",
          "|---|---|---|---|---|---|---|"]
    for e, names in MODE_NAMES.items():
        L.append(f"| {e} | " + " | ".join(names + ["-"] * (6 - len(names))) + " |")
    L.append("")
    L += doc_v2()
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    with open(os.path.join(ROOT, "docs", "DMX.md"), "w") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    write_inc()
    write_gdtf()
    write_doc()
    for name, mode, _ in MODES_DEF:
        print(name, layout(mode)[1], "channels")
    print("Full v2", layout2()[1], "channels")
