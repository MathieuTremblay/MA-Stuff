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
VERSION = "1.0"

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
    (170, "Nothing"),
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
    with open(os.path.join(ROOT, "src", "dmx_profile.inc"), "w") as f:
        f.write("\n".join(lines))


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


def write_gdtf():
    root = ET.Element("GDTF", DataVersion="1.1")
    ft = sub(root, "FixtureType", Name="TouchFX", ShortName="TouchFX", LongName="TouchFX touch visuals",
             Manufacturer="TouchFX", Description="Touch-driven visuals with NDI output. Basic and Advanced modes.",
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
    for g, feats in groups.items():
        e = sub(fg, "FeatureGroup", Name=g, Pretty=g)
        for feat in feats:
            sub(e, "Feature", Name=feat)
    attrs = sub(ad, "Attributes")
    for a, (pretty, unit, act, main, col) in ATTR_INFO.items():
        g, feat = FEATURES[a]
        sub(attrs, "Attribute", Name=a, Pretty=pretty, ActivationGroup=act, Feature=f"{g}.{feat}",
            MainAttribute=main, PhysicalUnit=unit or "None", Color=col)
    sub(ft, "Wheels")
    sub(ft, "PhysicalDescriptions")
    sub(ft, "Models")
    geos = sub(ft, "Geometries")
    ident = "{1,0,0,0}{0,1,0,0}{0,0,1,0}{0,0,0,1}"
    body = sub(geos, "Geometry", Name="Body", Position=ident)
    for i, child in enumerate(["Low", "Mid", "High", "Cursor1", "Cursor2"]):
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


# ---------------------------------------------------------------------------
def write_doc():
    L = ["# TouchFX DMX profiles", "",
         "Generated by `tools/dmx_profile.py`. The GDTF file `gdtf/TouchFX@TouchFX.gdtf` contains both modes.", "",
         "Default patch: **sACN universe 100, address 1** (Art-Net: universe 99, since Art-Net counts from 0).", "",
         "Discrete channels (effect, scene, palette, mirror, speed, mode) use the same layout: "
         f"**0-9 = keep** (no change, the panel stays in charge), then equal slots from {SLOT_START} to {SLOT_END}, "
         "**250-255 = keep** (on the Advanced palette channel: **use the DMX colours** from the Low/Mid/High instances).",
         "", "TouchFX follows *last change wins*: a channel only takes over when its DMX value changes, so the touch "
         "panel and the console can both be used. With **DMX LOCK** on, DMX always wins.", ""]
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
    os.makedirs(os.path.join(ROOT, "docs"), exist_ok=True)
    with open(os.path.join(ROOT, "docs", "DMX.md"), "w") as f:
        f.write("\n".join(L))


if __name__ == "__main__":
    write_inc()
    write_gdtf()
    write_doc()
    for name, mode, _ in MODES_DEF:
        print(name, layout(mode)[1], "channels")
