#!/usr/bin/env python3
"""Sends test DMX to TouchFX over sACN and Art-Net (no console needed).

  python3 tools/dmx_test_sender.py [target_ip] [seconds] [--poll]

Default target 127.0.0.1 (same PC). Sends at 40 Hz, both protocols at once:
  sACN universe 100  and  Art-Net universe 99 (same data)
  Advanced mode at address 1:   Lasers / Tunnel, DMX colours red-green-blue, two cursors circling, 128 BPM
  Basic mode at address 101:    Sparkles, palette 7, quad mirror, shutter strobe, 80% dimmer
"""
import math
import os
import socket
import struct
import sys
import time
import uuid

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dmx_profile import ADVANCED, BASIC, F, SLOT_START, layout, slot_size  # noqa: E402

ARGS = [a for a in sys.argv[1:] if not a.startswith("--")]
POLL = "--poll" in sys.argv  # also act like a console doing Art-Net discovery, and print TouchFX's reply
TARGET = ARGS[0] if len(ARGS) > 0 else "127.0.0.1"
DURATION = float(ARGS[1]) if len(ARGS) > 1 else 1e9
SACN_UNIVERSE, ARTNET_UNIVERSE = 100, 99
CID = uuid.uuid4().bytes


def slot(index, count):
    size = slot_size(count)
    return SLOT_START + index * size + size // 2


def put(frame, mode, address, values):
    lay, _ = layout(mode)
    for fid, _geo, off in lay:
        if fid not in values:
            continue
        v = int(values[fid])
        a = address - 1 + off
        if F[fid]["bytes"] == 2:
            frame[a], frame[a + 1] = (v >> 8) & 255, v & 255
        else:
            frame[a] = v & 255


def bpm16(bpm):
    return 256 + int((bpm - 20) / 280 * (65535 - 256))


def sacn_packet(seq, data):
    n = len(data)
    pkt = bytearray()
    pkt += struct.pack("!HH12s", 0x0010, 0x0000, b"ASC-E1.17\x00\x00\x00")
    pkt += struct.pack("!HI16s", 0x7000 | (110 + n), 0x00000004, CID)
    pkt += struct.pack("!HI64sBHBBH", 0x7000 | (88 + n), 0x00000002, b"TouchFX test sender".ljust(64, b"\0"), 100, 0,
                       seq & 255, 0, SACN_UNIVERSE)
    pkt += struct.pack("!HBBHHH", 0x7000 | (11 + n), 0x02, 0xA1, 0x0000, 0x0001, n + 1)
    pkt += b"\x00" + bytes(data)
    return bytes(pkt)


def artnet_packet(seq, data):
    return b"Art-Net\x00" + struct.pack("<H", 0x5000) + struct.pack("!H", 14) + bytes(
        [seq & 255, 0, ARTNET_UNIVERSE & 255, (ARTNET_UNIVERSE >> 8) & 0x7F]) + struct.pack("!H", len(data)) + bytes(data)


def poll_socket():
    """Art-Net replies go to port 6454 of the poller. On the same PC, bind a second loopback address so
    the reply reaches this script and not TouchFX itself."""
    p = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    p.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    p.bind(("127.0.0.2" if TARGET.startswith("127.") else "0.0.0.0", 6454))
    p.setblocking(False)
    return p


def art_poll():
    return b"Art-Net\x00" + struct.pack("<H", 0x2000) + struct.pack("!H", 14) + bytes([0x02, 0x00])


def art_poll_reply_console():
    r = bytearray(239)
    r[0:8] = b"Art-Net\x00"
    r[8:10] = struct.pack("<H", 0x2100)
    r[26:26 + 18] = b"grandMA3".ljust(18, b"\0")
    r[44:44 + 64] = b"grandMA3 onPC (test)".ljust(64, b"\0")
    return bytes(r)


def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ps = poll_socket() if POLL else None
    last_poll = 0
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    t0 = time.time()
    seq = 0
    print(f"Sending to {TARGET}: sACN universe {SACN_UNIVERSE}, Art-Net universe {ARTNET_UNIVERSE}")
    while time.time() - t0 < DURATION:
        t = time.time() - t0
        frame = [0] * 512
        put(frame, ADVANCED, 1, {
            "DIMMER": 255, "EFFECT": slot(3, 6), "FXMODE": slot(4, 8), "PALETTE_ADV": 255, "MIRROR": slot(0, 6),
            "BPM": bpm16(128), "SHUTTER": 0, "PARAM1": 140, "PARAM2": 160, "PARAM3": 90, "PARAM4": 70, "GLOW": 190,
            "SATURATION": 170, "LOW_R": 255, "LOW_G": 0, "LOW_B": 0, "MID_R": 0, "MID_G": 255, "MID_B": 0,
            "HIGH_R": 0, "HIGH_G": 60, "HIGH_B": 255,
            "CUR1_DIM": 255, "CUR1_X": 32768 + 18000 * math.cos(t * 1.3), "CUR1_Y": 32768 + 15000 * math.sin(t * 1.3),
            "CUR2_DIM": 255, "CUR2_X": 32768 + 22000 * math.cos(-t * 0.9 + 2), "CUR2_Y": 32768 + 12000 * math.sin(-t * 0.9 + 2),
        })
        put(frame, BASIC, 101, {
            "DIMMER": 204, "EFFECT": slot(5, 6), "PALETTE": slot(6, 10), "MIRROR": slot(3, 6), "BPM": 0,
            "SHUTTER": 60, "PARAM1": 220, "GLOW": 160,
        })
        if ps and time.time() - last_poll > 2:
            last_poll = time.time()
            ps.sendto(art_poll_reply_console(), (TARGET, 6454))
            ps.sendto(art_poll(), (TARGET, 6454))
        if ps:
            try:
                data, addr = ps.recvfrom(1024)
                if data[:8] == b"Art-Net\x00" and struct.unpack("<H", data[8:10])[0] == 0x2100:
                    short = data[26:44].split(b"\0")[0].decode()
                    report = data[108:172].split(b"\0")[0].decode()
                    print(f"ArtPollReply from {addr[0]}: {short!r}, ip {'.'.join(map(str, data[10:14]))}, "
                          f"net {data[18]} sub {data[19]} uni {data[190]}, report {report!r}", flush=True)
            except BlockingIOError:
                pass
        s.sendto(sacn_packet(seq, frame), (TARGET, 5568))
        s.sendto(artnet_packet(seq, frame), (TARGET, 6454))
        seq += 1
        time.sleep(0.025)


if __name__ == "__main__":
    main()
