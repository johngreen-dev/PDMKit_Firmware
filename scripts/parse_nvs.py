#!/usr/bin/env python3
"""Parse an ESP-IDF NVS partition binary and print all key-value entries."""

import json
import os
import shutil
import struct
import subprocess
import sys

NVS_ADDR = "0x9000"
NVS_SIZE  = "0x6000"

PAGE_SIZE        = 4096
HEADER_SIZE      = 32
BITMAP_SIZE      = 32
ENTRY_SIZE       = 32
ENTRIES_PER_PAGE = (PAGE_SIZE - HEADER_SIZE - BITMAP_SIZE) // ENTRY_SIZE  # 126

# nvs_type_t values from nvs.h
TYPE_U8        = 0x01
TYPE_I8        = 0x11
TYPE_U16       = 0x02
TYPE_I16       = 0x12
TYPE_U32       = 0x04
TYPE_I32       = 0x14
TYPE_U64       = 0x08
TYPE_I64       = 0x18
TYPE_STR       = 0x21
TYPE_BLOB      = 0x41
TYPE_BLOB_DATA = 0x42
TYPE_BLOB_IDX  = 0x48

TYPE_NAMES = {
    TYPE_U8:        "u8",
    TYPE_I8:        "i8",
    TYPE_U16:       "u16",
    TYPE_I16:       "i16",
    TYPE_U32:       "u32",
    TYPE_I32:       "i32",
    TYPE_U64:       "u64",
    TYPE_I64:       "i64",
    TYPE_STR:       "str",
    TYPE_BLOB:      "blob",
    TYPE_BLOB_IDX:  "blob",
}

STATE_WRITTEN = 0b10
STATE_EMPTY   = 0b11


def entry_state(bitmap: bytes, idx: int) -> int:
    bit = idx * 2
    return (bitmap[bit // 8] >> (bit % 8)) & 0b11


def decode_key(raw: bytes) -> str:
    end = raw.find(b"\x00")
    return raw[:end].decode("utf-8", errors="replace") if end >= 0 else raw.decode("utf-8", errors="replace")


def parse(path: str) -> list:
    with open(path, "rb") as f:
        data = f.read()

    namespaces: dict[int, str] = {}
    rows = []

    for page_idx in range(len(data) // PAGE_SIZE):
        base = page_idx * PAGE_SIZE
        page = data[base: base + PAGE_SIZE]

        # Skip blank/uninitialized pages
        if struct.unpack_from("<I", page, 0)[0] == 0xFFFFFFFF:
            continue

        bitmap  = page[HEADER_SIZE: HEADER_SIZE + BITMAP_SIZE]
        entries = page[HEADER_SIZE + BITMAP_SIZE:]

        i = 0
        while i < ENTRIES_PER_PAGE:
            if entry_state(bitmap, i) != STATE_WRITTEN:
                i += 1
                continue

            e       = entries[i * ENTRY_SIZE: (i + 1) * ENTRY_SIZE]
            ns_idx  = e[0]
            e_type  = e[1]
            span    = e[2] or 1
            key     = decode_key(e[8:24])

            # Namespace definition: ns_idx=0, type=U8, data[0]=assigned index
            if ns_idx == 0x00 and e_type == TYPE_U8:
                namespaces[e[24]] = key
                i += span
                continue

            # Skip internal blob data chunks
            if e_type in (TYPE_BLOB_DATA,):
                i += span
                continue

            ns = namespaces.get(ns_idx, f"ns#{ns_idx}")

            if e_type == TYPE_U8:
                rows.append((ns, key, "u8", e[24]))
            elif e_type == TYPE_I8:
                rows.append((ns, key, "i8", struct.unpack_from("<b", e, 24)[0]))
            elif e_type == TYPE_U16:
                rows.append((ns, key, "u16", struct.unpack_from("<H", e, 24)[0]))
            elif e_type == TYPE_I16:
                rows.append((ns, key, "i16", struct.unpack_from("<h", e, 24)[0]))
            elif e_type == TYPE_U32:
                rows.append((ns, key, "u32", struct.unpack_from("<I", e, 24)[0]))
            elif e_type == TYPE_I32:
                rows.append((ns, key, "i32", struct.unpack_from("<i", e, 24)[0]))
            elif e_type == TYPE_U64:
                rows.append((ns, key, "u64", struct.unpack_from("<Q", e, 24)[0]))
            elif e_type == TYPE_I64:
                rows.append((ns, key, "i64", struct.unpack_from("<q", e, 24)[0]))
            elif e_type == TYPE_STR:
                length = struct.unpack_from("<H", e, 24)[0]
                raw = b"".join(
                    entries[(i + s) * ENTRY_SIZE: (i + s + 1) * ENTRY_SIZE]
                    for s in range(1, span)
                )
                rows.append((ns, key, "str", raw[:length - 1].decode("utf-8", errors="replace")))
            elif e_type == TYPE_BLOB_IDX:
                blob_len = struct.unpack_from("<I", e, 24)[0]
                rows.append((ns, key, "blob", f"<{blob_len} bytes>"))

            i += span

    return rows


def _find_esptool() -> list:
    exe = shutil.which("esptool") or shutil.which("esptool.exe")
    if exe:
        return [exe]
    for candidate in (sys.executable, "python", "python3"):
        if shutil.which(candidate):
            return [candidate, "-m", "esptool"]
    return [sys.executable, "-m", "esptool"]


def read_storage(port: str, out_path: str) -> None:
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    cmd = _find_esptool() + ["--port", port, "--baud", "921600",
                              "read_flash", NVS_ADDR, NVS_SIZE, out_path]
    print(f"Reading NVS from {port} …")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("esptool failed — aborting.")
        sys.exit(result.returncode)
    print(f"Saved to {out_path}\n")


def _format_value(typ: str, val) -> str:
    if typ == "str":
        try:
            parsed = json.loads(val)
            return json.dumps(parsed, indent=2)
        except (json.JSONDecodeError, TypeError):
            pass
    return str(val)


def main():
    import argparse
    ap = argparse.ArgumentParser(description="Parse ESP-IDF NVS partition binary.")
    ap.add_argument("--port", metavar="COM", help="Serial port — reads flash before parsing")
    ap.add_argument("file", nargs="?", default=os.path.join(
        os.path.dirname(__file__), "..", "storage_dump", "nvs.bin"
    ))
    args = ap.parse_args()

    if args.port:
        read_storage(args.port, args.file)

    if not os.path.exists(args.file):
        print(f"File not found: {args.file}")
        sys.exit(1)

    rows = parse(args.file)
    if not rows:
        print("No NVS entries found (partition is empty or unrecognised format).")
        return

    col = {"ns": 20, "key": 20, "type": 8}
    header = f"{'Namespace':<{col['ns']}} {'Key':<{col['key']}} {'Type':<{col['type']}} Value"
    print("\n" + header)
    print("-" * 72)
    for ns, key, typ, val in rows:
        formatted = _format_value(typ, val)
        lines = formatted.splitlines()
        if len(lines) == 1:
            print(f"{ns:<{col['ns']}} {key:<{col['key']}} {typ:<{col['type']}} {lines[0]}")
        else:
            print(f"{ns:<{col['ns']}} {key:<{col['key']}} {typ:<{col['type']}}")
            for line in lines:
                print(f"  {line}")
    print()


if __name__ == "__main__":
    main()
