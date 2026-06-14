#!/usr/bin/env python3
"""Apply the bundled SMW MSU-1 IPS patch to a stock SMW ROM, for regen.

The MSU-1 build is recompiled from an MSU-1-patched ROM: the patch injects a
~520-byte streaming-audio driver into bank $04 freespace ($04:EF46) and hooks
it from banks $00/$04 (recomp/bank04.cfg emits the driver). Rather than make
the user patch their ROM by hand, regen applies the bundled IPS (recomp/msu1/)
to their *stock* ROM in a throwaway file and recompiles from that. The user
only ever provides — and, at runtime, only needs — the stock ROM.

The patch's IPS offsets assume the canonical US ROM (unheadered, 512 KiB). If
the supplied ROM's SHA-256 doesn't match, patching may produce a broken ROM,
so we warn loudly (but still proceed, in case it's an acceptable variant).

WHICH PATCH: this is Conn's "Super Mario World MSU-1" (the audio-only / native
music-replacement patch — NOT the MSU-1(+) gameplay hack, NOT MSU-1 Plus
Ultra). PCM packs MUST be the set built for THIS patch; packs for the other two
SMW MSU-1 patches will not line up. See recomp/msu1/ATTRIBUTION.md.

Usage:
    python tools/apply_msu_patch.py --rom smw.sfc \
        --ips recomp/msu1/smw_msu.ips --out .build/smw_msu1.sfc
"""
import argparse
import hashlib
import sys

# Canonical "Super Mario World (USA).sfc", 512 KiB, unheadered — the ROM the
# bundled IPS targets (CRC32 0xB19ED489).
VANILLA_US_SHA256 = "0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b"


def rom_sha256(data: bytes) -> str:
    # Match the launcher: strip a 512-byte SMC copier header if present.
    hdr = 512 if (len(data) % 1024) == 512 else 0
    return hashlib.sha256(data[hdr:]).hexdigest()


def apply_ips(rom: bytearray, ips: bytes) -> int:
    if ips[:5] != b"PATCH":
        raise ValueError("not an IPS file (missing PATCH magic)")
    i, records = 5, 0
    while True:
        if ips[i:i + 3] == b"EOF":
            break
        off = (ips[i] << 16) | (ips[i + 1] << 8) | ips[i + 2]
        i += 3
        size = (ips[i] << 8) | ips[i + 1]
        i += 2
        if size == 0:  # RLE record
            run = (ips[i] << 8) | ips[i + 1]
            val = ips[i + 2]
            i += 3
            end = off + run
            if end > len(rom):
                rom.extend(b"\x00" * (end - len(rom)))
            for j in range(off, end):
                rom[j] = val
        else:
            end = off + size
            if end > len(rom):
                rom.extend(b"\x00" * (end - len(rom)))
            rom[off:end] = ips[i:i + size]
            i += size
        records += 1
    return records


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", required=True, help="stock SMW (USA) ROM")
    ap.add_argument("--ips", required=True, help="MSU-1 IPS patch")
    ap.add_argument("--out", required=True, help="output patched ROM")
    ap.add_argument("--expect-sha256", default=VANILLA_US_SHA256,
                    help="ROM hash the patch targets (for the mismatch warning)")
    args = ap.parse_args()

    rom = bytearray(open(args.rom, "rb").read())
    # Patch offsets are unheadered; strip a copier header before patching too.
    hdr = 512 if (len(rom) % 1024) == 512 else 0
    if hdr:
        del rom[:hdr]
    got = rom_sha256(bytes(rom))
    if args.expect_sha256 and got != args.expect_sha256:
        sys.stderr.write(
            "\n*** WARNING: MSU-1 patch / ROM mismatch ***\n"
            f"  {args.rom}\n"
            f"    sha256 : {got}\n"
            f"    expected: {args.expect_sha256}  (US, the patch's target)\n"
            "  The MSU-1 IPS patch is written for that exact ROM. Applying it to\n"
            "  a different ROM may produce a broken image and a non-working build.\n"
            "  Proceeding anyway.\n\n")

    ips = open(args.ips, "rb").read()
    n = apply_ips(rom, ips)
    open(args.out, "wb").write(rom)
    print(f"[apply_msu_patch] applied {n} IPS records -> {args.out} "
          f"({len(rom)} bytes, sha256 {rom_sha256(bytes(rom))})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
