#!/usr/bin/env -S uv run --quiet --with pyserial python
"""Read the display's USB-Serial-JTAG console. Usage: serial_capture.py [seconds] [file]
The port only emits while DTR is asserted (pyserial asserts it on open); a reset is not triggered here.
Reset separately (usb_flash.sh ends with a hard reset) and start this right after to catch most of the boot."""
import glob, sys, time
import serial
ports = glob.glob('/dev/cu.usbmodem*'); secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
out = sys.argv[2] if len(sys.argv) > 2 else 'serial.log'
if not ports: print('no /dev/cu.usbmodem* port'); sys.exit(1)
print('port', ports[0], 'capturing', secs, 's ->', out)
with serial.Serial(ports[0], 115200, timeout=0.5) as s, open(out, 'ab') as f:
    end = time.time() + secs; n = 0
    while time.time() < end:
        d = s.read(4096)
        if d: f.write(d); f.flush(); n += len(d)
print('captured', n, 'bytes')
