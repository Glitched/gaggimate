#!/usr/bin/env python3
"""Static internal-RAM budget of an ESP32-S3 image, from the linker map.

    python3 scripts/ram_budget.py [.pio/build/display/firmware.map] [--top N]

Internal SRAM on the S3 is shared between instruction RAM and data RAM: every byte of code
placed in IRAM (interrupt handlers, flash-safe code, the BLE controller, FreeRTOS) is a byte
that the DRAM heap can never have, except the first 16 KB which live in SRAM0 next to the
instruction cache. The linker records that mirror as `.dram0.dummy`. Together with `.data`
and `.bss` it is the part of the heap budget that is decided at link time; the runtime part
(task stacks, driver pools, LVGL, ...) is what GET /api/debug/heap reports.

Prints the three sections attributed to the archive or source directory that contributed them.
Informational only: it exits 0 whatever the numbers are.
"""
import argparse
import collections
import os
import re
import sys

SECTIONS = (".iram0.text", ".dram0.data", ".dram0.bss")
IRAM_UNMIRRORED = 16 * 1024  # SRAM0 share of IRAM that does not cost DRAM


def owner(path):
    m = re.search(r"([^/]+)\.a\(", path)
    if m:
        name = m.group(1)
        if name.startswith("libespressif__"):
            return "idf:" + name[len("libespressif__"):]
        if name == "libFrameworkArduino":
            return "arduino:core"
        return "idf:" + (name[3:] if name.startswith("lib") else name)
    m = re.search(r"\.pio/build/[^/]+/src/([^/]+)/([^/]+)/", path)
    if m:
        return "src:" + m.group(1) + "/" + m.group(2)
    m = re.search(r"\.pio/build/[^/]+/src/([^/]+)/", path)
    if m:
        return "src:" + m.group(1)
    m = re.search(r"\.pio/build/[^/]+/lib[^/]*/([^/]+)/", path)
    if m:
        return "lib:" + m.group(1)
    m = re.search(r"\.pio/build/[^/]+/([^/]+)/", path)
    if m:
        return m.group(1)
    return os.path.basename(path)


def parse(map_path):
    per_section = {s: collections.Counter() for s in SECTIONS}
    output_sizes = {}
    current = None
    pending = None
    with open(map_path, errors="replace") as fh:
        for line in fh:
            if line.strip() and not line.startswith(" "):
                parts = line.split()
                current = parts[0] if parts[0] in SECTIONS else None
                if parts[0].startswith((".iram0", ".dram0")) and len(parts) >= 3:
                    try:
                        output_sizes[parts[0]] = int(parts[2], 16)
                    except ValueError:
                        pass
                continue
            if current is None:
                continue
            parts = line.split()
            if pending is not None:
                parts = [pending] + parts
                pending = None
            if len(parts) == 1 and parts[0].startswith("."):
                pending = parts[0]  # long input-section name: address and size are on the next line
                continue
            if len(parts) >= 4 and parts[0].startswith(".") and parts[1].startswith("0x"):
                try:
                    size = int(parts[2], 16)
                except ValueError:
                    continue
                if size:
                    per_section[current][owner(" ".join(parts[3:]))] += size
    return per_section, output_sizes


def kb(n):
    return f"{n / 1024:6.1f} KB"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("map", nargs="?", default=".pio/build/display/firmware.map")
    ap.add_argument("--top", type=int, default=15, help="rows per section (default 15)")
    args = ap.parse_args()
    if not os.path.exists(args.map):
        sys.exit(f"{args.map}: not found (build the env first; PlatformIO writes the map next to firmware.elf)")
    per_section, sizes = parse(args.map)

    iram = sizes.get(".iram0.text", 0) + sizes.get(".iram0.vectors", 0)
    mirrored = sizes.get(".dram0.dummy", max(iram - IRAM_UNMIRRORED, 0))
    data = sizes.get(".dram0.data", 0)
    bss = sizes.get(".dram0.bss", 0)
    print(f"{os.path.relpath(args.map)}")
    print(f"  IRAM code            {kb(iram)}  of which mirrored into DRAM {kb(mirrored)}")
    print(f"  .data                {kb(data)}")
    print(f"  .bss                 {kb(bss)}")
    print(f"  static DRAM total    {kb(mirrored + data + bss)}  (decided at link time; the rest of the budget is runtime heap)")

    labels = {".iram0.text": "IRAM code", ".dram0.data": ".data", ".dram0.bss": ".bss"}
    for section in SECTIONS:
        counter = per_section[section]
        total = sum(counter.values())
        print(f"\n{labels[section]} by contributor ({kb(total).strip()} attributed):")
        for name, size in counter.most_common(args.top):
            print(f"  {kb(size)}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
