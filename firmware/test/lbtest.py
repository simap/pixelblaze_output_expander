#!/usr/bin/env python3
# Loopback test driver. Drives the firmware's loopback hook over SWD with
# OpenOCD and prints stats deltas per test. The hook sends from a PIO UART
# transmitter on GP20 into the UART0 receiver on GP19, which share the data
# input net, so no wiring beyond the debug probe is needed.
#
# Usage: lbtest.py [test ...]
# Tests: rates noise other_board bad_crc flood tolerance. Default runs all.
#
# Environment: PICO_SDK_OPENOCD (default ~/.pico-sdk/openocd/0.12.0+dev),
# PICO_TOOLCHAIN (default ~/.pico-sdk/toolchain/15_2_Rel1/bin),
# ELF (default ../build/outputexpander.elf). Symbol addresses for the
# loopback and stats structs come from the ELF.
import subprocess, struct, zlib, random, sys, time, os, colorsys, tempfile

SP = os.path.dirname(os.path.abspath(__file__))
OCD_DIR = os.path.expanduser(os.environ.get("PICO_SDK_OPENOCD", "~/.pico-sdk/openocd/0.12.0+dev"))
OCD = os.path.join(OCD_DIR, "openocd")
SCRIPTS = os.path.join(OCD_DIR, "scripts")
TOOLCHAIN = os.path.expanduser(os.environ.get("PICO_TOOLCHAIN", "~/.pico-sdk/toolchain/15_2_Rel1/bin"))
ELF = os.environ.get("ELF", os.path.join(SP, "..", "build", "outputexpander.elf"))
TMP = tempfile.mkdtemp(prefix="lbtest-")

def symbol(name):
    out = subprocess.run([os.path.join(TOOLCHAIN, "arm-none-eabi-nm"), ELF], capture_output=True, text=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise SystemExit(f"symbol {name} not found in {ELF}")

LB = symbol("loopback")
ST = symbol("stats")
LOOPBACK_MAX = 32768

# loopback layout
L_TRIG, L_LEN, L_REPEAT, L_BAUD, L_CHUNK, L_GAP, L_RXBAUD, L_MODE, L_SPIHZ, L_EXTRA, L_SENT, L_DATA = range(0, 48, 4)

STAT_NAMES = ["uartBytes", "spiBytes", "huntBytes", "frameMisses", "badHeaders", "crcErrors",
              "otherBoard", "drawCount", "overDraw", "rxOverruns", "uartErrors", "input", "busId", "baud"]

def ocd(cmds, cfg=None):
    args = [OCD, "-s", SCRIPTS, "-f", "interface/cmsis-dap.cfg", "-f", "target/rp2350.cfg",
            "-c", "adapter speed 5000", "-c", "init"]
    if cfg:
        args += ["-f", cfg]
    for c in cmds:
        args += ["-c", c]
    args += ["-c", "shutdown"]
    r = subprocess.run(args, capture_output=True, text=True)
    return r.stdout + r.stderr

def parse_words(out, addr, n):
    words = []
    for line in out.splitlines():
        if line.startswith("0x"):
            a, rest = line.split(":", 1)
            words += [int(w, 16) for w in rest.split()]
    return words

def read_stats():
    out = ocd([f"mdw 0x{ST:08x} 9"])
    w = parse_words(out, ST, 9)
    if len(w) < 9:
        raise SystemExit("stats read failed:\n" + out)
    d = {}
    d["uartBytes"], d["spiBytes"], d["huntBytes"] = w[0], w[1], w[2]
    d["frameMisses"], d["badHeaders"] = w[3] & 0xffff, w[3] >> 16
    d["crcErrors"], d["otherBoard"] = w[4] & 0xffff, w[4] >> 16
    d["drawCount"], d["overDraw"] = w[5] & 0xffff, w[5] >> 16
    d["rxOverruns"], d["uartErrors"] = w[6] & 0xffff, w[6] >> 16
    d["input"], d["busId"] = w[7] & 0xff, (w[7] >> 8) & 0xff
    d["baud"] = w[8]
    return d

def frame(channel, rtype, hdr=b"", payload=b""):
    f = b"UPXL" + bytes([channel, rtype]) + hdr + payload
    return f + struct.pack("<I", zlib.crc32(f) & 0xffffffff)

def send(data, repeat=1, baud=0, chunk=0, gap=0, rxbaud=0, mode=0, spihz=0, extra=0, timeout=60):
    assert len(data) <= LOOPBACK_MAX
    busy = parse_words(ocd([f"mdw 0x{LB:08x} 1"]), LB, 1)
    if busy and busy[0]:
        raise SystemExit("loopback hook still busy from a previous send, reset the target")
    cfg = os.path.join(TMP, "send.cfg")
    with open(cfg, "w") as f:
        f.write(f"write_memory 0x{LB + L_DATA:08x} 8 {{{' '.join(str(b) for b in data)}}}\n")
        for off, v in ((L_LEN, len(data)), (L_REPEAT, repeat), (L_BAUD, baud), (L_CHUNK, chunk), (L_GAP, gap), (L_RXBAUD, rxbaud),
                       (L_MODE, mode), (L_SPIHZ, spihz), (L_EXTRA, extra)):
            f.write(f"write_memory 0x{LB + off:08x} 32 {v}\n")
        f.write(f"write_memory 0x{LB:08x} 32 1\n")
    ocd([], cfg)
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.3)
        w = parse_words(ocd([f"mdw 0x{LB:08x} 1"]), LB, 1)
        if w and w[0] == 0:
            return True
    return False

def delta(a, b):
    out = {}
    for k in STAT_NAMES:
        if k in ("input", "busId", "baud"):
            out[k] = b[k]
        else:
            out[k] = (b[k] - a[k]) & 0xffffffff
            if k not in ("uartBytes", "spiBytes", "huntBytes"):
                out[k] &= 0xffff
    return out

def report(name, expect, d):
    fails = [f"{k}={d[k]} expected {v}" for k, v in expect.items() if d[k] != v]
    tag = "PASS" if not fails else "FAIL"
    print(f"{tag} {name}: uart={d['uartBytes']} spi={d['spiBytes']} draws={d['drawCount']} over={d['overDraw']} crc={d['crcErrors']} "
          f"uartErr={d['uartErrors']} overrun={d['rxOverruns']} hunt={d['huntBytes']} miss={d['frameMisses']} "
          f"bad={d['badHeaders']} other={d['otherBoard']} baud={d['baud']}")
    for f in fails:
        print("     ", f)
    sys.stdout.flush()

def run(name, data, expect, **kw):
    s0 = read_stats()
    ok = send(data, **kw)
    s1 = read_stats()
    d = delta(s0, s1)
    if not ok:
        print(f"TIMEOUT {name}")
    report(name, expect, d)
    return d

ORDER = 0x24
HZ = 10000000

def rgb(h, s=1.0, v=1.0):
    r, g, b = colorsys.hsv_to_rgb(h % 1.0, s, v)
    return bytes([int(r * 255), int(g * 255), int(b * 255)])

def apa_small(f=0, n=9):
    px = b"".join(rgb(f / 60 + i / n) + bytes([8]) for i in range(n))
    return frame(1, 3, struct.pack("<IBBH", HZ, ORDER, 0, n), px)

clk = frame(0, 4, struct.pack("<I", HZ))
draw = frame(0, 2)

def test_rates():
    px = bytes((i * 7) & 0xff for i in range(4000 * 4))
    big = clk + frame(1, 3, struct.pack("<IBBH", HZ, ORDER, 0, 4000), px) + draw
    for baud in (2000000, 3000000, 4000000, 5000000):
        rep = 100
        run(f"rate {baud} big frames x{rep}", big, {"uartBytes": len(big) * rep, "drawCount": rep, "crcErrors": 0, "uartErrors": 0, "rxOverruns": 0, "overDraw": 0, "baud": baud},
            repeat=rep, rxbaud=baud, baud=baud, timeout=120)

def test_tolerance():
    data = clk + b"".join(apa_small(f) + draw for f in range(60))
    rep = 20
    for pct in (-5, -4, -3.5, -3, -2.5, -2, -1, 0, 1, 2, 2.5, 3, 3.5, 4, 5):
        tx = int(2000000 * (1 + pct / 100))
        d = run(f"tolerance tx {pct:+}% ({tx})", data, {}, repeat=rep, rxbaud=2000000, baud=tx, timeout=60)
        print(f"      {pct:+}%: draws {d['drawCount']} of {60 * rep}")

def test_noise():
    random.seed(1)
    data = clk
    frames = 0
    while True:
        chunk = apa_small(frames) + draw
        junk = bytes(random.randrange(256) for _ in range(random.randrange(0, 120)))
        if len(data) + len(chunk) + len(junk) > LOOPBACK_MAX:
            break
        data += chunk + junk
        frames += 1
    rep = 20
    run(f"noise, {frames} frames per pass with random junk x{rep}", data, {"drawCount": frames * rep, "crcErrors": 0, "uartErrors": 0, "rxOverruns": 0}, repeat=rep, rxbaud=2000000)

def test_other_board():
    data = clk
    for bus in range(1, 8):
        data += frame((bus << 3) | 1, 3, struct.pack("<IBBH", HZ, ORDER, 0, 9), bytes(36))
    data += draw
    rep = 10
    run(f"other board x{rep}", data, {"otherBoard": 7 * rep, "drawCount": rep, "crcErrors": 0}, repeat=rep)

def test_bad_crc():
    data = clk
    bad = 0
    for f in range(40):
        fr = bytearray(apa_small(f))
        if f % 2:
            fr[20] ^= 0x01
            bad += 1
        data += bytes(fr) + draw
    rep = 10
    run(f"bad crc x{rep}", data, {"crcErrors": bad * rep, "drawCount": 40 * rep, "uartErrors": 0}, repeat=rep)

def test_flood():
    data = draw * 800
    rep = 50
    run(f"draw flood at 5 Mbaud x{rep}", data, {"uartBytes": len(data) * rep, "crcErrors": 0, "rxOverruns": 0, "uartErrors": 0}, repeat=rep, rxbaud=5000000, baud=5000000)
    run("restore 2 Mbaud", clk + apa_small() + draw, {"drawCount": 1, "baud": 2000000}, rxbaud=2000000, baud=2000000)

def test_visual():
    # One lit pixel walks down both strips, 2 steps per second, 10 seconds,
    # then both strips go dark. APA102 on channels 0 and 1, WS2812 on 2.
    data = b""
    chunk = None
    for step in range(20):
        apa = b"".join((bytes([255, 0, 0, 8]) if i == step % 9 else bytes(4)) for i in range(9))
        ws = b"".join((bytes([0, 255, 0]) if i == step % 4 else bytes(3)) for i in range(4))
        chunk = clk + frame(1, 3, struct.pack("<IBBH", 1000000, ORDER, 0, 9), apa)
        chunk += frame(2, 1, struct.pack("<BBH", 3, ORDER, 4), ws) + draw
        data += chunk
    off = clk + frame(1, 3, struct.pack("<IBBH", 1000000, ORDER, 0, 9), bytes(36))
    off += frame(2, 1, struct.pack("<BBH", 3, ORDER, 4), bytes(12)) + draw
    assert len(off) == len(chunk)
    data += off
    run("visual walk", data, {"drawCount": 21, "crcErrors": 0}, chunk=len(chunk), gap=500000, timeout=60)

def test_spi_rates():
    px = bytes((i * 7) & 0xff for i in range(4000 * 4))
    big = clk + frame(1, 3, struct.pack("<IBBH", HZ, ORDER, 0, 4000), px) + draw
    for hz in (1000000, 2000000, 5000000, 10000000, 12500000, 20000000, 25000000):
        rep = 50
        # Above 10 MHz the frames arrive faster than a 4000 pixel draw at
        # 10 MHz completes, so some DRAW_ALLs are overdraws by design.
        d = run(f"spi rate {hz} big frames x{rep}", big, {"spiBytes": len(big) * rep, "crcErrors": 0, "rxOverruns": 0, "input": 1},
                repeat=rep, mode=1, spihz=hz, timeout=30)
        if d["drawCount"] + d["overDraw"] != rep:
            print(f"      FAIL draws {d['drawCount']} + overdraws {d['overDraw']} != {rep}")

def test_spi_noise():
    random.seed(2)
    data = clk
    frames = 0
    while True:
        chunk = apa_small(frames) + draw
        junk = bytes(random.randrange(256) for _ in range(random.randrange(0, 120)))
        if len(data) + len(chunk) + len(junk) > LOOPBACK_MAX:
            break
        data += chunk + junk
        frames += 1
    rep = 10
    run(f"spi noise, {frames} frames per pass with random junk x{rep}", data, {"drawCount": frames * rep, "crcErrors": 0, "rxOverruns": 0}, repeat=rep, mode=1, spihz=10000000)

def test_spi_realign():
    data = clk + apa_small() + draw
    # Leave 4 bits of a byte in the receiver, then send a whole frame set
    # after the idle timeout. Without the realign the second set is shifted
    # by 4 bits and fails.
    run("spi partial byte then frame", data, {"drawCount": 1, "crcErrors": 0}, mode=1, spihz=10000000, extra=4)
    run("spi frame after realign", data, {"drawCount": 1, "crcErrors": 0, "huntBytes": 0}, mode=1, spihz=10000000)
    # Pixel at a time: a pause after every 4 payload bytes, longer than the
    # idle timeout, must not break the frame.
    run("spi pixel at a time with 300 us gaps", data, {"drawCount": 1, "crcErrors": 0}, mode=1, spihz=10000000, chunk=4, gap=300)

def test_spi_switch():
    data = clk + apa_small() + draw
    run("spi then uart while spi is live", data, {"drawCount": 1, "input": 1}, mode=1, spihz=5000000)
    # UART frames are discarded while the SPI ring is live.
    run("uart frame ignored during spi hold", data, {"drawCount": 0, "input": 1}, mode=0)
    time.sleep(1.2)
    run("uart frame after spi hold expires", data, {"drawCount": 1, "input": 0}, mode=0)

if __name__ == "__main__":
    tests = sys.argv[1:] or ["rates", "noise", "other_board", "bad_crc", "flood", "tolerance"]
    for t in tests:
        globals()["test_" + t]()
