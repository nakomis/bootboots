#!/usr/bin/env python3
"""BOOT-10: checks for provision.sh, with no dependencies beyond the standard library.

  prov_image.py verify <prov.bin> <size> [idf-nvs-tool-dir]
      Parses the NVS image: every non-empty page's header CRC, every written entry's header CRC
      and every string's data CRC; a free page left for NVS's garbage collection; and every key
      the firmware needs present as a string in the "prov" namespace. If ESP-IDF's own
      nvs_partition_tool directory is given (and exists), its integrity check runs as well; an
      exception or a reported problem from it fails the check. Prints key names, never values.

  prov_image.py table <partitions.bin> <offset>
      Succeeds if the binary partition table has a data/nvs partition called "prov" at <offset>.

Exit status 0 only when the check returns True.
"""
import contextlib
import io
import os
import struct
import sys
import zlib

PAGE = 4096
ENTRY = 32
ENTRIES_PER_PAGE = 126
FIRST_ENTRY = 64
PAGE_ACTIVE, PAGE_FULL, PAGE_EMPTY = 0xFFFFFFFE, 0xFFFFFFFC, 0xFFFFFFFF
TYPE_U8, TYPE_SZ = 0x01, 0x21
REQUIRED = {"thing", "wifi_ssid", "wifi_pass", "iot_endpoint", "cred_endpoint", "api_host",
            "role_alias", "region", "env", "root_ca", "cert", "key"}


def crc(data):
    return zlib.crc32(bytes(data), 0xFFFFFFFF) & 0xFFFFFFFF


def entry_state(bitmap, i):
    return (bitmap[(i * 2) // 8] >> ((i * 2) % 8)) & 0b11   # 0b10 written, 0b11 empty, 0b00 erased


def verify(image):
    """True if the image is a sound NVS partition with every required key; else a reason."""
    if len(image) % PAGE:
        return f"size {len(image)} isn't a whole number of 4 KB pages"
    namespaces, strings, free_pages = {}, {}, 0
    for p in range(len(image) // PAGE):
        page = image[p * PAGE:(p + 1) * PAGE]
        state = struct.unpack_from("<I", page, 0)[0]
        if state == PAGE_EMPTY:
            if page != b"\xff" * PAGE:
                return f"page {p} is marked empty but holds data"
            free_pages += 1
            continue
        if state not in (PAGE_ACTIVE, PAGE_FULL):
            return f"page {p} has an unexpected state {state:#x}"
        if crc(page[4:28]) != struct.unpack_from("<I", page, 28)[0]:
            return f"page {p} header CRC mismatch"
        bitmap = page[32:64]
        i = 0
        while i < ENTRIES_PER_PAGE:
            if entry_state(bitmap, i) != 0b10:
                i += 1
                continue
            e = page[FIRST_ENTRY + i * ENTRY:FIRST_ENTRY + (i + 1) * ENTRY]
            ns, typ, span = e[0], e[1], e[2]
            if crc(e[0:4] + e[8:32]) != struct.unpack_from("<I", e, 4)[0]:
                return f"page {p} entry {i} header CRC mismatch"
            key = e[8:24].split(b"\0", 1)[0].decode("ascii", "replace")
            if span < 1 or i + span > ENTRIES_PER_PAGE:
                return f"page {p} entry {i} ({key}) has a bad span {span}"
            if ns == 0 and typ == TYPE_U8:
                namespaces[e[24]] = key
            elif typ == TYPE_SZ:
                size, data_crc = struct.unpack_from("<H", e, 24)[0], struct.unpack_from("<I", e, 28)[0]
                start = FIRST_ENTRY + (i + 1) * ENTRY
                data = page[start:start + size]
                if size > (span - 1) * ENTRY or crc(data) != data_crc:
                    return f"page {p} string {key} data CRC mismatch"
                if size == 0 or data[-1:] != b"\0":
                    return f"page {p} string {key} isn't NUL-terminated"
                strings[(ns, key)] = size
            i += span
    if free_pages < 1:
        return "no free page left for NVS garbage collection"
    prov = [idx for idx, name in namespaces.items() if name == "prov"]
    if not prov:
        return "no 'prov' namespace"
    present = {key for (ns, key) in strings if ns == prov[0]}
    missing = REQUIRED - present
    if missing:
        return "missing " + ", ".join(sorted(missing))
    print(f"prov image OK: {len(image)} bytes, namespace 'prov', keys {', '.join(sorted(present))}")
    return True


def idf_integrity(path, tool_dir):
    """True if ESP-IDF's nvs_partition_tool reports no problems; else a reason."""
    sys.path.insert(0, tool_dir)
    try:
        import nvs_check, nvs_logger, nvs_parser  # noqa: E401
        nvs = nvs_parser.NVS_Partition(os.path.basename(path), bytearray(open(path, "rb").read()))
        log = nvs_logger.NVS_Logger(color="always")
        report = io.StringIO()
        with contextlib.redirect_stdout(report), contextlib.redirect_stderr(report):
            result = nvs_check.integrity_check(nvs, log)
    except Exception as exc:  # any failure of the checker fails the check
        return f"ESP-IDF's NVS integrity check raised {type(exc).__name__}"
    text = report.getvalue()
    # integrity_check reports through its logger (red/yellow lines) and returns None; anything
    # else it returns is treated as a verdict.
    if result not in (None, True):
        return f"ESP-IDF's NVS integrity check returned {result!r}"
    if nvs_logger.NVS_Logger.ansi["red"] in text or nvs_logger.NVS_Logger.ansi["yellow"] in text:
        return "ESP-IDF's NVS integrity check reported a problem"
    return True


def table(image, offset):
    for pos in range(0, len(image) - ENTRY + 1, ENTRY):
        e = image[pos:pos + ENTRY]
        if e[0:2] != b"\xaa\x50":
            break   # 0xFFFF (end) or the MD5 entry (0xEBEB)
        ptype, subtype, off, size = e[2], e[3], *struct.unpack_from("<II", e, 4)
        label = e[12:28].split(b"\0", 1)[0].decode("ascii", "replace")
        if label == "prov":
            if ptype == 0x01 and subtype == 0x02 and off == offset:
                return True
            return f"prov is type {ptype:#x}/{subtype:#x} at {off:#x}, not data/nvs at {offset:#x}"
    return f"no prov partition in the table"


def main(argv):
    if len(argv) >= 4 and argv[1] == "verify":
        path, size = argv[2], int(argv[3], 0)
        image = open(path, "rb").read()
        result = verify(image) if len(image) == size else f"{len(image)} bytes, not the partition's {size}"
        if result is True and len(argv) > 4 and os.path.isdir(argv[4]):
            result = idf_integrity(path, argv[4])
            if result is True:
                print("ESP-IDF NVS integrity check: OK")
    elif len(argv) == 4 and argv[1] == "table":
        result = table(open(argv[2], "rb").read(), int(argv[3], 0))
    else:
        print(__doc__, file=sys.stderr)
        return 2
    if result is not True:
        print(f"prov_image: {result}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
