#!/usr/bin/env python3
"""Freeze the owner-ROM recipe; players need no Python installation."""
import argparse
import importlib.metadata
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    args.output.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    subprocess.run([
        sys.executable, "-m", "PyInstaller", "--noconfirm", "--clean",
        "--onefile", "--name", "smw-falcon-cache",
        "--distpath", str(args.output.resolve()),
        "--workpath", str(args.work.resolve() / "build"),
        "--specpath", str(args.work.resolve()),
        "--exclude-module", "tkinter", "--exclude-module", "numpy",
        str(source / "build_final_cache.py"),
    ], check=True)
    notices = args.output / "falcon-cache-licenses"
    notices.mkdir(exist_ok=True)
    for package in ("Pillow", "PyInstaller"):
        dist = importlib.metadata.distribution(package)
        copied = 0
        for entry in dist.files or ():
            if ".dist-info/" in str(entry) and any(
                part.lower().startswith(("license", "copying")) for part in entry.parts
            ):
                path = Path(dist.locate_file(entry))
                if path.is_file():
                    shutil.copyfile(path, notices / f"{package}-{path.name}")
                    copied += 1
        if not copied:
            raise RuntimeError(f"Missing license for {package}")
    python_notices = [
        Path(sys.base_prefix) / "LICENSE.txt",
        Path(f"/usr/share/doc/python{sys.version_info.major}.{sys.version_info.minor}/copyright"),
    ]
    python_license = next((p for p in python_notices if p.is_file()), None)
    if python_license is None:
        raise RuntimeError("Cannot locate Python redistribution license")
    shutil.copyfile(python_license, notices / "Python.txt")


if __name__ == "__main__":
    main()
