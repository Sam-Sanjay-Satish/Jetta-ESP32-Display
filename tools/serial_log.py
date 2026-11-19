#!/usr/bin/env python3
"""Read a serial port for N seconds and save it to a log file.
Usage: serial_log.py PORT SECONDS LOGFILE [--no-reset]
By default it taps the reset line first so you also see the boot messages."""
import sys, time
import serial

port, secs, logfile = sys.argv[1], float(sys.argv[2]), sys.argv[3]
s = serial.Serial(port, 115200, timeout=0.2)
if "--no-reset" not in sys.argv:
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False
end = time.time() + secs
with open(logfile, "w") as f:
    while time.time() < end:
        d = s.read(4096)
        if d:
            t = d.decode("utf-8", "replace")
            f.write(t); f.flush()
            sys.stdout.write(t); sys.stdout.flush()
