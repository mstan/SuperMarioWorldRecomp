#!/usr/bin/env python3
"""Build the complete, immutable Captain Falcon owner-ROM cache.

This is the one entry point intended for a release launcher.  Given the
already selected US 1.0 Smash 64 owner ROM, it runs the Falcon-only reloc
extractor, model/animation decoder, runtime baker, and eleven-cue audio
renderer.  Its final output is an *external*, content-addressed directory:

    falcon_runtime.bin
    audio/<each of the eleven approved cue names>.wav
    manifest.json

The ROM is held only while a stage executes.  Neither its path nor a copy of
its bytes is written to a cache manifest.  Intermediates exist only below a
same-volume temporary directory and are deleted before the final directory is
verified and atomically published.  Final caches are immutable: rebuilding a
matching ROM/recipe produces the same content-addressed directory, so an
existing good cache is never renamed out from under a running game.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import time
import wave
from pathlib import Path
from typing import Any


TOOLS_ROOT = Path(__file__).resolve().parent
IS_FROZEN = bool(getattr(sys, "frozen", False) or getattr(sys, "_MEIPASS", None))
SOURCE_ROOT = None if IS_FROZEN else Path(__file__).resolve().parents[2]
if str(TOOLS_ROOT) not in sys.path:
    sys.path.insert(0, str(TOOLS_ROOT))
if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

import bake_falcon_runtime  # noqa: E402
import build_cache  # noqa: E402
import decode_intermediates  # noqa: E402
import owner_audio  # noqa: E402


FINAL_FORMAT = "smw-smash64-falcon-owner-cache"
FINAL_RECIPE_VERSION = 1
RUNTIME_FILENAME = "falcon_runtime.bin"
APPROVED_RUNTIME_SHA256 = "8a8e0ac01341584488dad5681ae7563f3142ee15915ff154f5e3122c57146a3e"

# This is deliberately a complete, ordered inventory rather than a permissive
# glob.  PCM hashes validate what the runtime actually plays; the file hashes
# additionally make the outer cache manifest resistant to altered WAV headers.
EXPECTED_AUDIO: dict[str, tuple[int, str]] = {
    "falcon_dive_catch_fgm.wav": (6593, "854e7149472e8a57fba488013ee43e7a7418aa676ff34f5c02d1b2be681963ad"),
    "falcon_dive_explosion_fgm.wav": (30396, "13d4cf67808b38c38807d17766e625a41c0aa6ac7364e16b90270388daa34cc4"),
    "falcon_dive_launch_fgm.wav": (8945, "f1c39a977653576dafa3b1dcf555045b72085db959bae339ebe57511c30b01d9"),
    "falcon_dive_voice.wav": (30683, "25c37937c449dc7ee1e2121db0aeb443fa20cf5155916b3a1152641968a94e98"),
    "falcon_jump_effort.wav": (9614, "4a625de1e79ce193e105bb08b9307c0b06ede6f7dc0dab1ed5c35a154accaf30"),
    "falcon_kick.wav": (36559, "965a076958350f649d13117d983cf0b8a791082facdd9835f42e6ac7981a7989"),
    "falcon_kick_start_fgm.wav": (35754, "637ebd882d912d2a7e4b4d466c3c83e5b02b3e2e81f5f32b16307077340ddd94"),
    "falcon_kick_swing_fgm.wav": (8249, "0bd5f000cb8a363be1de4ed114963c5377dc196d52584b79e048e8dcf7a4a128"),
    "falcon_punch_falcon.wav": (24873, "b6ab6eb87bc95e7b38acaeb796e338f646bc1b7eb250a063013455a3158d8922"),
    "falcon_punch_impact_fgm.wav": (49735, "05daa8a3791a37b89d4327d7e4b99b8ecf3079c1023a1e8e8ceb0fd257caf436"),
    "falcon_punch_punch.wav": (41366, "496ac5d0762572fc1a649431c2aadb462effabda2d937d6ca39e1647cee803f6"),
}


def default_cache_root() -> Path:
    local = os.environ.get("LOCALAPPDATA")
    if local:
        return Path(local) / "SuperMarioWorldRecomp" / "smash64"
    xdg = os.environ.get("XDG_CACHE_HOME")
    if xdg and Path(xdg).is_absolute():
        return Path(xdg) / "SuperMarioWorldRecomp" / "smash64"
    return Path.home() / ".cache" / "SuperMarioWorldRecomp" / "smash64"


def _require_external(path: Path) -> Path:
    """Resolve a cache path and reject every source-tree spelling of it."""
    try:
        resolved = path.resolve()
    except OSError as exc:
        raise ValueError(f"cannot resolve cache path: {exc}") from exc
    if SOURCE_ROOT is not None and (resolved == SOURCE_ROOT or SOURCE_ROOT in resolved.parents):
        raise ValueError("owner-derived cache must be outside the source tree")
    return resolved


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _read_wav(path: Path) -> tuple[int, str]:
    """Return strict PCM frame count/hash without trusting a 44-byte layout."""
    try:
        with wave.open(str(path), "rb") as stream:
            if (stream.getnchannels(), stream.getsampwidth(), stream.getframerate(),
                    stream.getcomptype()) != (1, 2, owner_audio.OUTPUT_RATE, "NONE"):
                raise ValueError(f"{path.name} is not 44.1 kHz mono PCM16")
            frames = stream.getnframes()
            pcm = stream.readframes(frames)
    except (wave.Error, EOFError) as exc:
        raise ValueError(f"invalid Falcon WAV {path.name}: {exc}") from exc
    if len(pcm) != frames * 2:
        raise ValueError(f"truncated Falcon WAV {path.name}")
    return frames, hashlib.sha256(pcm).hexdigest()


def _expected_relative_files() -> set[str]:
    return {"manifest.json", RUNTIME_FILENAME} | {f"audio/{name}" for name in EXPECTED_AUDIO}


def _relative_file_inventory(root: Path) -> set[str]:
    return {item.relative_to(root).as_posix() for item in root.rglob("*") if item.is_file()}


def _assert_final_tree_shape(root: Path) -> None:
    """Reject links and even empty unexpected directories before opening files."""
    if not root.is_dir() or root.is_symlink():
        raise ValueError("final Falcon cache is not a real directory")
    entries = list(root.rglob("*"))
    if any(item.is_symlink() for item in entries):
        raise ValueError("final Falcon cache must not contain symbolic links")
    directories = {item.relative_to(root).as_posix() for item in entries if item.is_dir()}
    if directories != {"audio"}:
        raise ValueError("final Falcon cache contains unexpected directories")


def _manifest_bytes(manifest: dict[str, Any]) -> bytes:
    return (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")


def _write_manifest(root: Path, rom_sha1: str) -> dict[str, Any]:
    runtime = root / RUNTIME_FILENAME
    if not runtime.is_file() or _sha256(runtime) != APPROVED_RUNTIME_SHA256:
        raise ValueError("Falcon runtime blob does not match the approved visual/animation asset")
    audio_entries = []
    for name, (expected_frames, expected_pcm_hash) in sorted(EXPECTED_AUDIO.items()):
        clip = root / "audio" / name
        if not clip.is_file():
            raise ValueError(f"missing Falcon audio cue {name}")
        frames, pcm_hash = _read_wav(clip)
        if (frames, pcm_hash) != (expected_frames, expected_pcm_hash):
            raise ValueError(f"Falcon audio cue {name} does not match the approved PCM")
        audio_entries.append({"file": f"audio/{name}", "frames": frames,
                              "pcm_sha256": pcm_hash, "sha256": _sha256(clip)})
    manifest: dict[str, Any] = {
        "format": FINAL_FORMAT,
        "recipe_version": FINAL_RECIPE_VERSION,
        "normalized_rom_sha1": rom_sha1,
        "runtime": {"file": RUNTIME_FILENAME, "sha256": APPROVED_RUNTIME_SHA256},
        "audio": audio_entries,
    }
    (root / "manifest.json").write_bytes(_manifest_bytes(manifest))
    return manifest


def verify_final_cache(path: Path, expected_rom_sha1: str | None = None) -> dict[str, Any]:
    """Fail closed unless this is exactly a complete approved final cache."""
    root = _require_external(path)
    _assert_final_tree_shape(root)
    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        raise ValueError("final Falcon cache has no manifest")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError("final Falcon cache manifest is unreadable") from exc
    if manifest.get("format") != FINAL_FORMAT or manifest.get("recipe_version") != FINAL_RECIPE_VERSION:
        raise ValueError("unsupported Falcon final-cache recipe")
    rom_sha1 = manifest.get("normalized_rom_sha1")
    if not isinstance(rom_sha1, str) or len(rom_sha1) != 40:
        raise ValueError("final Falcon cache has no normalized ROM identity")
    if expected_rom_sha1 is not None and rom_sha1 != expected_rom_sha1:
        raise ValueError("final Falcon cache belongs to a different owner ROM")
    if _relative_file_inventory(root) != _expected_relative_files():
        raise ValueError("final Falcon cache contains missing or unexpected files")
    runtime = manifest.get("runtime")
    if runtime != {"file": RUNTIME_FILENAME, "sha256": APPROVED_RUNTIME_SHA256}:
        raise ValueError("final Falcon runtime manifest does not match the approved asset")
    if _sha256(root / RUNTIME_FILENAME) != APPROVED_RUNTIME_SHA256:
        raise ValueError("final Falcon runtime blob hash mismatch")
    audio = manifest.get("audio")
    if not isinstance(audio, list) or len(audio) != len(EXPECTED_AUDIO):
        raise ValueError("final Falcon audio manifest is incomplete")
    seen: set[str] = set()
    for entry in audio:
        if not isinstance(entry, dict):
            raise ValueError("invalid Falcon audio manifest entry")
        relative = entry.get("file")
        if not isinstance(relative, str) or not relative.startswith("audio/"):
            raise ValueError("invalid Falcon audio manifest filename")
        name = relative.removeprefix("audio/")
        if name in seen or name not in EXPECTED_AUDIO:
            raise ValueError("duplicate or unknown Falcon audio manifest entry")
        seen.add(name)
        frames, pcm_hash = _read_wav(root / relative)
        expected_frames, expected_pcm_hash = EXPECTED_AUDIO[name]
        if (frames, pcm_hash) != (expected_frames, expected_pcm_hash):
            raise ValueError(f"final Falcon audio cue {name} failed PCM validation")
        if entry != {"file": relative, "frames": frames, "pcm_sha256": pcm_hash,
                     "sha256": _sha256(root / relative)}:
            raise ValueError(f"final Falcon audio manifest hash mismatch for {name}")
    if seen != set(EXPECTED_AUDIO):
        raise ValueError("final Falcon audio cue inventory is incomplete")
    return manifest


def _validated_rom_sha1(rom_path: Path) -> str:
    """Validate in memory before even considering an existing cache."""
    normalized = build_cache.normalize_rom(rom_path.read_bytes())
    rom_sha1 = hashlib.sha1(normalized).hexdigest()
    if rom_sha1 != build_cache.CANONICAL_SHA1:
        raise ValueError("ROM is not the supported Smash Bros. US v1.0 image")
    return rom_sha1


def _existing_cache(cache_root: Path, rom_sha1: str) -> Path | None:
    prefix = f"falcon-final-r{FINAL_RECIPE_VERSION}-{rom_sha1}-"
    if not cache_root.is_dir():
        return None
    valid: list[Path] = []
    for candidate in cache_root.iterdir():
        if candidate.is_dir() and candidate.name.startswith(prefix):
            try:
                verify_final_cache(candidate, rom_sha1)
            except ValueError:
                continue
            valid.append(candidate)
    if len(valid) > 1:
        raise ValueError("multiple complete Falcon owner caches found; manual cache cleanup is required")
    return valid[0] if valid else None


def _publish_immutable(staging: Path, cache_root: Path, rom_sha1: str) -> Path:
    """Publish by no-replace directory rename; never evict an old good cache."""
    manifest_hash = _sha256(staging / "manifest.json")
    target = cache_root / f"falcon-final-r{FINAL_RECIPE_VERSION}-{rom_sha1}-{manifest_hash[:16]}"
    for attempt in range(4):
        try:
            # os.rename is intentionally used instead of os.replace: replacement
            # would make an existing validated cache briefly unavailable on Windows.
            os.rename(staging, target)
            return target
        except (FileExistsError, OSError):
            if not target.exists():
                if attempt == 3:
                    raise
                continue
            try:
                verify_final_cache(target, rom_sha1)
            except ValueError:
                quarantine = cache_root / (
                    f".{target.name}.corrupt-{time.time_ns()}-{os.getpid()}")
                try:
                    os.rename(target, quarantine)
                except FileNotFoundError:
                    continue
                continue
            shutil.rmtree(staging, ignore_errors=True)
            return target
    raise ValueError("could not publish the rebuilt Falcon owner cache")


def build_final_cache(rom_path: Path, cache_root: Path | None = None,
                      rebuild: bool = False) -> Path:
    """Build and atomically return an immutable verified external final cache."""
    root = _require_external(cache_root or default_cache_root())
    rom_sha1 = _validated_rom_sha1(rom_path)
    if not rebuild:
        existing = _existing_cache(root, rom_sha1)
        if existing is not None:
            return existing
    root.mkdir(parents=True, exist_ok=True)
    # The raw reloc cache is also external and independently integrity checked.
    # It is not placed below the final cache staging tree or shipped to users.
    try:
        reloc_cache = build_cache.build_cache(rom_path, root / "reloc", rebuild=rebuild)
    except ValueError:
        if rebuild:
            raise
        # A corrupt raw cache is never trusted, but it is recoverable. The raw
        # builder constructs and verifies replacement staging before moving the
        # invalid directory aside.
        reloc_cache = build_cache.build_cache(rom_path, root / "reloc", rebuild=True)
    staging = Path(tempfile.mkdtemp(prefix=".falcon-final-stage-", dir=root))
    try:
        intermediate = staging / "intermediate"
        decode_intermediates.materialize(reloc_cache, intermediate)
        bake_falcon_runtime.write_blob(intermediate, staging / RUNTIME_FILENAME)
        # owner_audio makes its own manifest while staged; validate it through
        # our stricter final inventory, then remove that nested transient file.
        owner_audio.render_audio(rom_path, staging / "audio")
        (staging / "audio" / "manifest.json").unlink()
        shutil.rmtree(intermediate)
        _write_manifest(staging, rom_sha1)
        verify_final_cache(staging, rom_sha1)
        return _publish_immutable(staging, root, rom_sha1)
    except Exception:
        shutil.rmtree(staging, ignore_errors=True)
        raise


def write_result_file(path: Path, cache: Path) -> None:
    """Atomically publish only the immutable cache directory basename."""
    target = _require_external(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    name = cache.name
    if not name.startswith(f"falcon-final-r{FINAL_RECIPE_VERSION}-") or any(
            separator in name for separator in ("/", "\\")):
        raise ValueError("refusing to publish an invalid Falcon cache name")
    temporary = target.with_name(f".{target.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(name + "\n", encoding="utf-8")
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rom", type=Path, help="owner Smash 64 US v1.0 ROM (.z64/.v64/.n64)")
    parser.add_argument("--cache-root", type=Path, default=default_cache_root(),
                        help="external user-writable cache root")
    parser.add_argument("--verify", type=Path, help="verify an existing final cache only")
    parser.add_argument("--result-file", type=Path,
                        help="atomically write the final cache directory basename here")
    parser.add_argument("--rebuild", action="store_true", help="re-run all stages even if a valid final cache exists")
    args = parser.parse_args(argv)
    try:
        if args.verify is not None:
            manifest = verify_final_cache(args.verify)
            print(f"verified {args.verify} (recipe {manifest['recipe_version']})")
            return 0
        if args.rom is None:
            parser.error("--rom is required unless --verify is used")
        output = build_final_cache(args.rom, args.cache_root, args.rebuild)
        if args.result_file is not None:
            write_result_file(args.result_file, output)
        print(output)
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"owner-ROM final cache error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
