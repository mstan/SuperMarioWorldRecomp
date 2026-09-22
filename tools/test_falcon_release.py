#!/usr/bin/env python3
"""Opt-in release probe. Requires local SMW and Smash US v1.0 owner ROMs.

Runs continuously with scripted input and isolated settings; retains screenshots,
controller traces, saves and logs for review. Never distributes owner assets.
For an AppImage, pass --catalog <build>/mods/preloaded/packages.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


BOOT = """wait 180
press start 1
wait 90
press start 1
wait 90
press start 1
wait 1450
press right 4
wait 90
press b 1
wait 150
wait 30
press right 20
wait 30
press b 2
wait 50
press y 2
wait 150
savestate 2
wait 30
press right 20
wait 30
press b 2
wait 50
press y 2
"""
RELOAD = """wait 240
loadstate 0
wait 60
press right 12
wait 30
press b 1
wait 80
press y 2
wait 60
savestate 2
"""


def run(args, name, enabled, save=None):
    out = args.out / name
    out.mkdir(parents=True)
    executable = args.exe
    if executable.suffix == ".AppImage":
        executable = out / args.exe.name
        shutil.copy2(args.exe, executable)
        executable.chmod(0o755)
    mods = out / "mods/preloaded"
    shutil.copytree(args.catalog, mods / "packages")
    state = 'format_version = 1\n'
    for package, feature, version, on in [
        ("super-mario-world.smash64.captain-falcon", "captain-falcon", "0.0.1", enabled),
        ("super-mario-world.enhancement.widescreen", "widescreen", "1.0.0", True),
    ]:
        state += (f'\n[[package]]\nid = "{package}"\nversion = "{version}"\n'
                  f'[[feature]]\npackage_id = "{package}"\nid = "{feature}"\n'
                  f'enabled = {str(on).lower()}\n')
        if feature == "widescreen":
            state += '[feature.values]\nmode = "adaptive"\nspawn = "adaptive"\n'
    state += ('\n[[resource]]\npackage_id = "super-mario-world.smash64.captain-falcon"\n'
              'feature_id = "captain-falcon"\nid = "smash64-us-v10"\n'
              f'path = {json.dumps(str(args.owner_rom))}\n')
    (mods / "state.toml").write_text(state, encoding="utf-8")
    config = out / "config.ini"
    config.write_text('[General]\nAutosave=0\n[Graphics]\nWindowSize=1280x720\n'
                      'NewRenderer=1\nNoSpriteLimits=1\nOutputMethod=SDL-Software\n'
                      '[Sound]\nEnableAudio=0\n[GamepadMap]\n'
                      'EnableGamepad1=false\nEnableGamepad2=false\n')
    (out / "saves").mkdir()
    if save:
        shutil.copyfile(save, out / "saves/save0.sav")
    script = out / "input.script"
    script.write_text(RELOAD if save else BOOT)
    env = os.environ.copy()
    for key in list(env):
        if key.startswith(("SNESRECOMP_FALCON_", "SNESRECOMP_LLE_BOUNCE",
                           "SMW_RENDER_", "SNESRECOMP_FORCE_", "SNESRECOMP_TURBO_")):
            del env[key]
    env.update(SDL_VIDEODRIVER="dummy", SDL_VIDEO_DRIVER="dummy",
               SDL_AUDIODRIVER="dummy", SDL_AUDIO_DRIVER="dummy",
               APPIMAGE_EXTRACT_AND_RUN="1",
               SNESRECOMP_FALCON_PRESENTATION_TRACE=str(out / "falcon.trace"),
               SNESRECOMP_FTRING_DUMP=str(out / "controller.csv"),
               SMW_RENDER_DIAGNOSTICS=str(out), SMW_RENDER_CAPTURE_EVERY="30",
               SNESRECOMP_FRAMEDUMP_START="241" if save else "2400",
               SNESRECOMP_FRAMEDUMP_END="600" if save else "2800",
               LOCALAPPDATA=str(args.cache), XDG_CACHE_HOME=str(args.cache))
    command = [str(executable), str(args.rom), "--config", str(config),
               "--script", str(script), "--framedump", str(out / "frames"),
               "--benchmark", "650" if save else "2800"]
    with (out / "stdout.log").open("w") as stdout, (out / "stderr.log").open("w") as stderr:
        subprocess.run(command, cwd=out, env=env, stdout=stdout, stderr=stderr,
                       timeout=120, check=True,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    frame = 570 if save else 2700
    ram = (out / "frames" / f"frame_{frame:06d}_wram.bin").read_bytes()
    assert ram[0x100] == 0x14, f"{name}: did not reach ordinary level gameplay"
    rows = list(csv.DictReader((out / "controller.csv").open()))
    trace = (out / "falcon.trace").read_text()
    if enabled:
        assert "approved runtime cache loaded" in trace and "mesh compositor active" in trace
        live = [r for r in rows if int(r["frame"]) > (250 if save else 2400)]
        assert len(live) > 30, f"{name}: controller hooks did not run"
        assert len({r["state"] for r in live}) > 1, f"{name}: input did not change controller state"
        assert max(float(r["x"]) for r in live) - min(float(r["x"]) for r in live) > 5
    else:
        assert not rows and "mesh compositor active" not in trace, f"{name}: disabled mod revived"
    assert (out / "saves/save2.sav").is_file()
    print(f"PASS {name}: stage, selection, controller and save checks; captures in {out}", flush=True)
    return out / "saves/save2.sav"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("exe", "rom", "owner-rom", "out"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--catalog", type=Path)
    args = parser.parse_args()
    for name in ("exe", "rom", "owner_rom", "out"):
        setattr(args, name, getattr(args, name).resolve())
    args.catalog = (args.catalog or args.exe.parent / "mods/preloaded/packages").resolve()
    # Keep derived assets outside the repository/package, even for cold testing.
    args.cache = Path(tempfile.mkdtemp(prefix="smw-falcon-release-"))
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / "cache-location.txt").write_text(str(args.cache))
    falcon = run(args, "cold-falcon", True)
    mario = run(args, "disabled-load-falcon", False, falcon)
    run(args, "enabled-load-mario", True, mario)


if __name__ == "__main__":
    main()
