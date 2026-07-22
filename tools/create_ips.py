#!/usr/bin/env python3
"""Create a deterministic IPS patch from two ROM images.

Copier headers are stripped automatically so the resulting IPS uses canonical
unheadered ROM offsets. The output is immediately applied in memory and
byte-compared with the target before it is written.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

from apply_msu_patch import apply_ips


def strip_copier_header(data: bytes) -> bytes:
    return data[512:] if len(data) % 1024 == 512 else data


def _same_byte_run(data: bytes, start: int, limit: int = 0xFFFF) -> int:
    end = min(len(data), start + limit)
    value = data[start]
    pos = start + 1
    while pos < end and data[pos] == value:
        pos += 1
    return pos - start


def create_ips(source: bytes, target: bytes) -> bytes:
    if len(target) > 0x1000000:
        raise ValueError("classic IPS cannot address files larger than 16 MiB")

    records = bytearray(b"PATCH")
    pos = 0
    highest_written = 0
    while pos < len(target):
        source_byte = source[pos] if pos < len(source) else 0
        if source_byte == target[pos]:
            pos += 1
            continue

        record_start = pos
        rle_len = _same_byte_run(target, pos)
        if rle_len >= 4:
            records += record_start.to_bytes(3, "big")
            records += b"\x00\x00"
            records += rle_len.to_bytes(2, "big")
            records.append(target[pos])
            pos += rle_len
            highest_written = max(highest_written, pos)
            continue

        literal = bytearray()
        while pos < len(target) and len(literal) < 0xFFFF:
            source_byte = source[pos] if pos < len(source) else 0
            if source_byte == target[pos]:
                break
            if literal and _same_byte_run(target, pos) >= 4:
                break
            literal.append(target[pos])
            pos += 1
        records += record_start.to_bytes(3, "big")
        records += len(literal).to_bytes(2, "big")
        records += literal
        highest_written = max(highest_written, pos)

    # IPS has no mandatory final-size field. Force expansion through the final
    # byte when a zero-filled tail would otherwise be indistinguishable from
    # bytes beyond the end of the source file.
    if len(target) > len(source) and highest_written < len(target):
        records += (len(target) - 1).to_bytes(3, "big")
        records += b"\x00\x01"
        records.append(target[-1])

    records += b"EOF"
    return bytes(records)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--target", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--expect-source-sha256")
    parser.add_argument("--expect-target-sha256")
    args = parser.parse_args()

    source = strip_copier_header(args.source.read_bytes())
    target = strip_copier_header(args.target.read_bytes())
    source_hash = sha256(source)
    target_hash = sha256(target)
    if args.expect_source_sha256 and source_hash != args.expect_source_sha256.lower():
        raise SystemExit(
            f"source SHA-256 mismatch: got {source_hash}, "
            f"expected {args.expect_source_sha256.lower()}"
        )
    if args.expect_target_sha256 and target_hash != args.expect_target_sha256.lower():
        raise SystemExit(
            f"target SHA-256 mismatch: got {target_hash}, "
            f"expected {args.expect_target_sha256.lower()}"
        )

    patch = create_ips(source, target)
    result = bytearray(source)
    count = apply_ips(result, patch)
    if bytes(result) != target:
        raise SystemExit("internal error: generated IPS does not reproduce target")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(patch)
    print(
        f"[create_ips] {count} records, {len(patch)} bytes -> {args.out}\n"
        f"  source: {len(source)} bytes, sha256 {source_hash}\n"
        f"  target: {len(target)} bytes, sha256 {target_hash}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
