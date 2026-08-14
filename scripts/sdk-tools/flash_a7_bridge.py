#!/usr/bin/env python3
"""PTY bridge: hold FT232 DTR/RTS reset, run dldtool on a pty, release after SYNC wait.

Board RESET is active-HIGH by default:
  pyserial False -> FTDI pin HIGH -> HOLD reset
  pyserial True  -> FTDI pin LOW  -> RELEASE
"""
from __future__ import annotations

import argparse
import os
import pty
import select
import subprocess
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=921600)
    ap.add_argument("--line", choices=("dtr", "rts", "both"), default="both")
    ap.add_argument("--polarity", choices=("high", "low"), default="high",
                    help="board reset polarity (high=拉高复位)")
    ap.add_argument("--hold-ms", type=int, default=200)
    ap.add_argument("--release-delay-ms", type=int, default=50,
                    help="after seeing Wait for SYNC, extra delay before release")
    ap.add_argument("--log", default="/tmp/flash_a7_bridge.log")
    ap.add_argument("dld_cmd", nargs=argparse.REMAINDER)
    args = ap.parse_args()
    cmd = args.dld_cmd
    if cmd and cmd[0] == "--":
        cmd = cmd[1:]
    if not cmd:
        print("need dldtool command after --", file=sys.stderr)
        return 2

    hold = False if args.polarity == "high" else True
    release = not hold

    ser = serial.Serial(
        port=args.port,
        baudrate=args.baud,
        timeout=0.05,
        write_timeout=2,
        dsrdtr=False,
        rtscts=False,
    )
    # idle released first
    ser.dtr = release
    ser.rts = release
    time.sleep(0.02)
    if args.line in ("dtr", "both"):
        ser.dtr = hold
    if args.line in ("rts", "both"):
        ser.rts = hold
    print(f"[bridge] HOLD {args.line} active-{args.polarity}", flush=True)
    time.sleep(args.hold_ms / 1000.0)

    master, slave = pty.openpty()
    slave_name = os.ttyname(slave)
    # Rewrite first /dev/ttyUSB* or given port arg in cmd to pty slave
    cmd2 = []
    replaced = False
    for a in cmd:
        if (not replaced) and (a == args.port or a.startswith("/dev/ttyUSB") or a.startswith("/dev/ttyACM")):
            cmd2.append(slave_name)
            replaced = True
        else:
            cmd2.append(a)
    if not replaced:
        print("[bridge] warning: did not find serial port arg to replace", flush=True)

    logf = open(args.log, "wb", buffering=0)
    print(f"[bridge] spawn: {' '.join(cmd2)}", flush=True)
    proc = subprocess.Popen(
        cmd2,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True,
    )
    os.close(slave)

    released = False
    buf = b""
    deadline = time.time() + 90
    try:
        while time.time() < deadline:
            if proc.poll() is not None and not select.select([master, ser], [], [], 0.05)[0]:
                break
            r, _, _ = select.select([master, ser], [], [], 0.1)
            if master in r:
                try:
                    data = os.read(master, 4096)
                except OSError:
                    data = b""
                if data:
                    logf.write(data)
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
                    buf += data
                    ser.write(data)
                    if (not released) and (b"Wait for SYNC" in buf or b"Wait for sync" in buf):
                        time.sleep(args.release_delay_ms / 1000.0)
                        ser.dtr = release
                        ser.rts = release
                        released = True
                        print(f"\n[bridge] RELEASE after SYNC wait", flush=True)
            if ser in r:
                data = ser.read(4096)
                if data:
                    os.write(master, data)
                    logf.write(data)
        # If SYNC never printed, release anyway once so boot can happen during retries
        if not released:
            ser.dtr = release
            ser.rts = release
            print("[bridge] RELEASE (fallback, no SYNC banner yet)", flush=True)
            # keep bridging a bit more
            end2 = time.time() + 25
            while time.time() < end2 and proc.poll() is None:
                r, _, _ = select.select([master, ser], [], [], 0.1)
                if master in r:
                    data = os.read(master, 4096)
                    if data:
                        logf.write(data)
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                        ser.write(data)
                if ser in r:
                    data = ser.read(4096)
                    if data:
                        os.write(master, data)
                        logf.write(data)
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        try:
            os.close(master)
        except OSError:
            pass
        ser.dtr = release
        ser.rts = release
        ser.close()
        logf.close()

    # Dump success hint
    try:
        text = open(args.log, "rb").read()
    except OSError:
        text = b""
    ok = b"PROGRAMMING SUCCEEDED" in text
    print(f"[bridge] exit={proc.returncode} ok={ok}", flush=True)
    return 0 if ok else (proc.returncode or 1)


if __name__ == "__main__":
    sys.exit(main())
