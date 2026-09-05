#!/usr/bin/env -S uv run --quiet --with pyserial python
"""Reset the display over USB-Serial-JTAG and capture its boot log. Usage: boot_capture.py [seconds] [file]
The T-RGB's console only emits while DTR is asserted and a pulse on RTS resets the chip, so: open with both low,
pulse RTS for 150 ms, then raise DTR and read. Catches everything from the ROM banner on (serial_capture.py reads
without resetting). This restarts the display; do not run it against a machine someone is using."""
import glob, sys, time
import serial
secs = float(sys.argv[1]) if len(sys.argv) > 1 else 30
out = sys.argv[2] if len(sys.argv) > 2 else 'boot.log'
ports = glob.glob('/dev/cu.usbmodem*')
if not ports: print('no /dev/cu.usbmodem* port'); sys.exit(1)
s = serial.Serial(); s.port = ports[0]; s.baudrate = 115200; s.timeout = 0.3; s.dtr = False; s.rts = False; s.open()
s.rts = True; time.sleep(0.15); s.rts = False; s.dtr = True
end = time.time() + secs; n = 0
with open(out, 'wb') as f:
    while time.time() < end:
        d = s.read(4096)
        if d: f.write(d); f.flush(); n += len(d)
print('captured', n, 'bytes from', ports[0], '->', out)
