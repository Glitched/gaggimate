#!/usr/bin/env python3
"""Attribute the display's live heap allocations to the code that made them.

    scripts/heap_trace_report.py [--host gaggimate.local] [--elf .pio/build/display-heaptrace/firmware.elf]
                                 [--top 30] [--input dump.txt] [--save dump.txt] [--addr2line PATH]

Needs firmware from the `display-heaptrace` env, which compiles in ESP-IDF's standalone heap tracer
and starts it before initArduino(). GET /api/debug/heap/trace lists every live block with the call
stack that allocated it (innermost frame first); this script symbolizes those frames against the
ELF of the same build and prints who owns internal RAM (the scarce 300 KB on the die) and who owns
PSRAM. POST /api/debug/heap/trace/reset first to see only what a shot or a page load adds.
"""
import argparse
import collections
import glob
import os
import re
import subprocess
import sys
import urllib.request

INTERNAL = ((0x3FC88000, 0x3FD00000), (0x600FE000, 0x60100000))  # SRAM, RTC fast memory
PSRAM = ((0x3C000000, 0x3E000000),)
# Frames that only forward an allocation; the owner is the first frame that is none of these.
SKIP = re.compile(
    r"^(heap_caps_|multi_heap|tlsf_|malloc$|calloc$|realloc$|free$|_malloc_r|_calloc_r|_realloc_r|operator new|"
    r"ps_malloc|ps_calloc|ps_realloc|__wrap_|String::|std::|__gnu_cxx::|ArduinoJson::|PsramAllocator::|"
    r"xTaskCreate|prvAllocateTask|pvPortMalloc|lv_mem_alloc|lv_mem_realloc|lv_malloc)"
)


def in_ranges(addr, ranges):
    return any(lo <= addr < hi for lo, hi in ranges)


def fetch(host):
    url = f"http://{host}/api/debug/heap/trace"
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            return resp.read().decode("utf-8", errors="replace")
    except Exception as exc:  # noqa: BLE001 - the message is the point
        sys.exit(f"{url}: {exc} (stock builds do not have this route; flash the display-heaptrace env)")


def parse(text):
    header, records = {}, []
    for line in text.splitlines():
        if not line.strip():
            continue
        if line.startswith("#"):
            header.update(kv.split("=", 1) for kv in line[1:].split() if "=" in kv)
            continue
        parts = line.split()
        try:
            size, addr = int(parts[0]), int(parts[1], 16)
            frames = [int(p, 16) for p in parts[2:]]
        except (IndexError, ValueError):
            continue
        records.append((size, addr, frames))
    return header, records


def find_addr2line(override):
    if override:
        return override
    for pattern in ("xtensa-esp-elf-addr2line", "xtensa-esp32s3-elf-addr2line"):
        hits = glob.glob(os.path.expanduser(f"~/.platformio/packages/toolchain-xtensa-esp-elf/bin/{pattern}"))
        if hits:
            return hits[0]
    sys.exit("addr2line not found under ~/.platformio/packages/toolchain-xtensa-esp-elf/bin; pass --addr2line")


def symbolize(addr2line, elf, pcs):
    """Map each PC to (function, file, line) with one addr2line run; '??' where the ELF has no answer."""
    pcs = sorted(pcs)
    if not pcs:
        return {}
    proc = subprocess.run([addr2line, "-f", "-C", "-e", elf], input="\n".join(f"0x{pc:08x}" for pc in pcs) + "\n",
                          capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        sys.exit(f"{addr2line} failed: {proc.stderr.strip()}")
    lines = proc.stdout.splitlines()
    table = {}
    for i, pc in enumerate(pcs):
        func = lines[2 * i].strip() if 2 * i < len(lines) else "??"
        loc = lines[2 * i + 1].strip() if 2 * i + 1 < len(lines) else "??:0"
        path, _, lineno = loc.rpartition(":")
        table[pc] = (func, path or "??", lineno.split()[0] if lineno else "0")
    return table


def library_key(func, path):
    if path and path != "??":
        p = path.replace("\\", "/")
        for marker, prefix in (("/src/display/", "src/display/"), ("/lib/", "lib:"), ("/libdeps/", "lib:"),
                               ("framework-arduinoespressif32/", "arduino:"), ("framework-espidf/components/", "idf:"),
                               ("esp-idf/components/", "idf:")):
            if marker in p:
                rest = p.split(marker, 1)[1]
                if prefix == "lib:" and marker == "/libdeps/":
                    rest = rest.split("/", 1)[1] if "/" in rest else rest  # skip the env directory
                return prefix + rest.split("/", 1)[0]
        return os.path.dirname(p) or p
    # Closed-source IDF libraries have symbols but no line info: group by the symbol's prefix.
    m = re.match(r"^(r_ble|ble|btdm|bt|r_)", func)
    if m:
        return "sym:bluetooth"
    if re.match(r"^(pp_|wifi_|ieee80211|esp_wifi|hal_|lmac|rc_|wdev|net80211|ppT|wDev)", func):
        return "sym:wifi"
    head = re.split(r"[_:(]", func, maxsplit=1)[0]
    return "sym:" + (head or func or "??")


def owner_frame(frames, symbols):
    """Innermost frame that is not an allocator wrapper; else the innermost known frame; else the innermost."""
    resolved = [(pc,) + symbols.get(pc, ("??", "??", "0")) for pc in frames]
    for pc, func, path, line in resolved:
        if func != "??" and not SKIP.match(func):
            return pc, func, path, line
    for pc, func, path, line in resolved:
        if func != "??":
            return pc, func, path, line
    return resolved[0] if resolved else (0, "??", "??", "0")


def short_path(path):
    if path == "??":
        return "??"
    for marker in ("/gaggimate-idf5/", "/gaggimate/", "/.platformio/packages/", "/libdeps/"):
        if marker in path:
            return path.split(marker, 1)[1]
    return path


def print_table(title, rows, top):
    print(f"\n{title}")
    print(f"  {'bytes':>8} {'count':>6}  {'function':<52} location")
    for (func, path, line), (size, count) in rows[:top]:
        loc = f"{short_path(path)}:{line}" if path != "??" else "(no line info)"
        print(f"  {size:>8} {count:>6}  {func[:52]:<52} {loc}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--host", default="gaggimate.local")
    ap.add_argument("--elf", default=".pio/build/display-heaptrace/firmware.elf")
    ap.add_argument("--top", type=int, default=30)
    ap.add_argument("--input", help="read a saved dump instead of fetching from the device")
    ap.add_argument("--save", help="write the raw dump here for a later --input run")
    ap.add_argument("--addr2line", help="path to xtensa addr2line (auto-detected from the PlatformIO toolchain)")
    args = ap.parse_args()

    if not os.path.exists(args.elf):
        sys.exit(f"{args.elf}: not found; build the display-heaptrace env (the ELF must match the running firmware)")
    addr2line = find_addr2line(args.addr2line)

    if args.input:
        with open(args.input, encoding="utf-8", errors="replace") as fh:
            text = fh.read()
    else:
        text = fetch(args.host)
    if args.save:
        with open(args.save, "w", encoding="utf-8") as fh:
            fh.write(text)
    header, records = parse(text)
    if "count" not in header:
        sys.exit("no trace header in the response: this firmware is not the display-heaptrace build "
                 "(a stock build serves the web app for unknown paths)")
    if not records:
        sys.exit("no records in the dump (tracing not running, or the buffer was just reset)")

    symbols = symbolize(addr2line, args.elf, {pc for _, _, frames in records for pc in frames})

    by_owner = {"internal": collections.defaultdict(lambda: [0, 0]), "psram": collections.defaultdict(lambda: [0, 0]),
                "other": collections.defaultdict(lambda: [0, 0])}
    by_lib = collections.defaultdict(lambda: [0, 0])
    totals = collections.defaultdict(lambda: [0, 0])
    for size, addr, frames in records:
        region = "internal" if in_ranges(addr, INTERNAL) else "psram" if in_ranges(addr, PSRAM) else "other"
        _, func, path, line = owner_frame(frames, symbols)
        entry = by_owner[region][(func, path, line)]
        entry[0] += size
        entry[1] += 1
        totals[region][0] += size
        totals[region][1] += 1
        if region == "internal":
            lib = by_lib[library_key(func, path)]
            lib[0] += size
            lib[1] += 1

    print("trace: " + "  ".join(f"{k}={v}" for k, v in header.items()))
    if header.get("overflowed") == "1":
        print("  WARNING: the record buffer overflowed; allocations made after that point are missing")
    for region in ("internal", "psram", "other"):
        size, count = totals[region]
        if count:
            print(f"  live {region:<8} {size:>9} bytes in {count:>5} blocks")

    ranked = lambda region: sorted(by_owner[region].items(), key=lambda kv: kv[1][0], reverse=True)  # noqa: E731
    print_table("Internal RAM by owner (innermost non-allocator frame):", ranked("internal"), args.top)
    print_table("PSRAM by owner:", ranked("psram"), 10)
    print("\nInternal RAM by library / directory:")
    for key, (size, count) in sorted(by_lib.items(), key=lambda kv: kv[1][0], reverse=True)[: args.top]:
        print(f"  {size:>8} {count:>6}  {key}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
