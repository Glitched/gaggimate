#!/bin/zsh
# USB flash to app0 with the OTA record erased so app0 boots. Usage: usb_flash.sh <firmware.bin>
# Uses PlatformIO's esptool; the port is resolved every time because the T-RGB renames it between enumerations.
P=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1); [ -z "$P" ] && { echo "no /dev/cu.usbmodem* port"; exit 1; }
ESPTOOL="$HOME/.platformio/penv/bin/python -m esptool --chip esp32s3 --port $P --baud 921600 --before default_reset"
eval $ESPTOOL --after no_reset erase-region 0xe000 0x2000 2>&1 | grep -iE 'erased|rror'
eval $ESPTOOL --after hard_reset write-flash 0x10000 "$1" 2>&1 | grep -E 'Wrote|verified|rror'
