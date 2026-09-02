#!/usr/bin/env python3
"""Verify the embedded web UI blob actually made it into the linked firmware.

web_ui_blob.S pulls the bundle in with ``.incbin``. Neither SCons nor the
compiler tracks that dependency: the build system decides whether to reassemble
by hashing the ``.S`` file, which does not change when the blob it references
does. So a ``web_ui_blob.S.o`` produced by an earlier stub build (1 byte, from
``pio run`` without ``build_webui.sh``) survives every later build, while
``web_ui_manifest.h`` -- an ordinary header -- updates normally.

The result is a firmware that links and boots but serves every asset out of
whatever rodata follows the 1-byte symbol: correct Content-Length, correct MIME
type, garbage body, no error anywhere. It is only visible over the network.
embed_webui.py now stamps the blob's size and digest into the ``.S`` so the
content changes with the blob, but this check is the backstop that makes the
failure impossible to ship. [GM-106]

Runs as a PlatformIO post-action on the ELF, and standalone:

    check_webui_blob.py <firmware.elf> <webassets_dir>

As a post-action, an intact but *empty* stub bundle (``pio run`` without
``build_webui.sh``) is only a warning by default, because a local build with no
UI is legitimate. Set ``GM_REQUIRE_WEBUI=1`` (CI does, for images that get
published) to turn that warning into a build failure.
"""

import hashlib
import os
import struct
import sys

SHT_SYMTAB = 2
SHT_NOBITS = 8

START_SYMBOL = "gWebUiBlobStart"
END_SYMBOL = "gWebUiBlobEnd"


class ElfError(Exception):
    pass


class Elf32:
    """Minimal little-endian ELF32 reader: section table + symbol lookup.

    Deliberately dependency-free so the check runs anywhere a build does,
    without pyelftools or a toolchain-specific nm/objcopy on PATH.
    """

    def __init__(self, data):
        if data[:4] != b"\x7fELF":
            raise ElfError("not an ELF file")
        if data[4] != 1:
            raise ElfError("expected a 32-bit ELF (EI_CLASS=%d)" % data[4])
        if data[5] != 1:
            raise ElfError("expected a little-endian ELF (EI_DATA=%d)" % data[5])
        self.data = data
        e_shoff, = struct.unpack_from("<I", data, 0x20)
        e_shentsize, e_shnum = struct.unpack_from("<HH", data, 0x2E)
        if not e_shoff or not e_shnum:
            raise ElfError("ELF has no section headers")
        self.sections = []
        for i in range(e_shnum):
            off = e_shoff + i * e_shentsize
            name, stype, _flags, addr, foff, size, link = struct.unpack_from("<IIIIIII", data, off)
            self.sections.append(
                {"name": name, "type": stype, "addr": addr, "offset": foff, "size": size, "link": link}
            )

    def _cstr(self, table_off, index):
        end = self.data.index(b"\0", table_off + index)
        return self.data[table_off + index : end].decode("utf-8", "replace")

    def symbols(self, wanted):
        """Return {name: address} for the requested symbol names."""
        found = {}
        for sec in self.sections:
            if sec["type"] != SHT_SYMTAB:
                continue
            strtab = self.sections[sec["link"]]["offset"]
            for off in range(sec["offset"], sec["offset"] + sec["size"], 16):
                st_name, st_value = struct.unpack_from("<II", self.data, off)
                if not st_name:
                    continue
                name = self._cstr(strtab, st_name)
                if name in wanted:
                    found[name] = st_value
        return found

    def read_at_addr(self, addr, length):
        """Read `length` bytes of loaded image content at virtual address `addr`."""
        for sec in self.sections:
            if sec["type"] == SHT_NOBITS or not sec["size"]:
                continue
            if sec["addr"] <= addr < sec["addr"] + sec["size"]:
                if addr + length > sec["addr"] + sec["size"]:
                    raise ElfError(
                        "blob at 0x%08x (%d bytes) runs past the end of its section" % (addr, length)
                    )
                start = sec["offset"] + (addr - sec["addr"])
                return self.data[start : start + length]
        raise ElfError("no section contains address 0x%08x" % addr)


def check(elf_path, assets_dir):
    """Return a list of human-readable problems; empty means the blob is intact."""
    blob_path = os.path.join(assets_dir, "web_ui.bin")
    if not os.path.isfile(blob_path):
        return ["%s is missing -- run scripts/build_webui.sh" % blob_path]

    with open(blob_path, "rb") as f:
        expected = f.read()
    with open(elf_path, "rb") as f:
        elf = Elf32(f.read())

    syms = elf.symbols({START_SYMBOL, END_SYMBOL})
    missing = sorted({START_SYMBOL, END_SYMBOL} - set(syms))
    if missing:
        return ["%s not found in %s -- web_ui_blob.S was not linked in" % (", ".join(missing), elf_path)]

    start, end = syms[START_SYMBOL], syms[END_SYMBOL]
    linked_size = end - start

    # gWebUiBlobEnd is followed by `.align 4`, but it is emitted immediately
    # after the .incbin, so end-start is the blob length exactly.
    if linked_size != len(expected):
        return [
            "linked blob is %d bytes but %s is %d bytes" % (linked_size, blob_path, len(expected)),
            "a stale web_ui_blob.S.o from a stub build is almost certainly cached",
            "fix: rm -f $BUILD_DIR/src/display/webassets/web_ui_blob.S.o && rebuild",
        ]

    linked = elf.read_at_addr(start, linked_size)
    if hashlib.sha256(linked).digest() != hashlib.sha256(expected).digest():
        return [
            "linked blob is the right size but its contents differ from %s" % blob_path,
            "the object file is stale; rebuild after removing web_ui_blob.S.o",
        ]
    return []


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    problems = check(argv[0], argv[1])
    if problems:
        print("check_webui_blob: FAILED", file=sys.stderr)
        for line in problems:
            print("  %s" % line, file=sys.stderr)
        return 1
    print("check_webui_blob: embedded web UI verified")
    return 0


# --- PlatformIO post-action -------------------------------------------------
if "Import" in globals():
    Import("env")  # noqa: F821 -- provided by PlatformIO/SCons

    def _verify(source, target, env):  # noqa: ARG001 -- SCons callback signature
        if env.subst("$ARDUINO_LIB_COMPILE_FLAG") == "Build":
            # pioarduino's hybrid build (custom_sdkconfig) first links a dummy sketch to
            # produce the IDF libraries; that ELF has no web bundle by design.
            print("check_webui_blob: skipped (IDF library compile pass)")
            return
        elf = str(target[0])
        assets = os.path.join(env["PROJECT_DIR"], "src", "display", "webassets")
        problems = check(elf, assets)
        if problems:
            print("\n*** Embedded web UI is corrupt -- refusing to produce a firmware image ***")
            for line in problems:
                print("    %s" % line)
            print("")
            env.Exit(1)
        with open(os.path.join(assets, "web_ui.bin"), "rb") as f:
            size = len(f.read())
        if size <= 1:
            if os.environ.get("GM_REQUIRE_WEBUI") == "1":
                print("\n*** Embedded web UI is the empty stub and GM_REQUIRE_WEBUI=1"
                      " -- refusing to produce a firmware image ***")
                print("    Run scripts/build_webui.sh before pio run, or unset GM_REQUIRE_WEBUI for a UI-less build.")
                print("")
                env.Exit(1)
            print("check_webui_blob: WARNING -- empty stub bundle linked in (no web UI).")
            print("                  Run scripts/build_webui.sh to embed the real bundle.")
        else:
            print("check_webui_blob: embedded web UI verified (%d bytes)" % size)

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _verify)  # noqa: F821

elif __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
