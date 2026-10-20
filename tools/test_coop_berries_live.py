#!/usr/bin/env python3
"""Exercise stock Map16 berry eating with controlled Yoshi-mouth competition.

Copied fixtures start from accepted native mount encounter states. This tool
stages actual Map16 berry tiles at stock mouth coordinates; it never sets berry
counters, mouth contents, eating timers, or reward/result flags. The original
ROM performs all bites, swallow behavior, rewards, and tile replacement.
The default alpha smoke pass is three cases on one view/backend. Additional
cases are opt-in diagnostics; their presence does not claim alpha acceptance.
`--expect` is optional so the same bounded cases can characterize a before-fix
build or assert the reviewed arbitration contract on a candidate build.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys

from coop_fixture import Fixture, ROOT


MOUNT_FIELDS = (
    0x0DC1, 0x13C7, 0x1410, 0x141E, 0x14A3,
    0x18AC, 0x18AD, 0x18AE, 0x18AF, 0x18B0, 0x18B1, 0x18B2, 0x18B3,
    0x18D4, 0x18D5, 0x18D6, 0x18DA, 0x18DE, 0x18E7, 0x18E8, 0x191C,
)

SPRITE_TABLES = (
    0x9E, 0xAA, 0xB6, 0xC2, 0xD8, 0xE4, 0x14C8, 0x14D4, 0x14E0,
    0x14EC, 0x14F8, 0x1504, 0x1510, 0x151C, 0x1528, 0x1534, 0x1540,
    0x154C, 0x1558, 0x1564, 0x1570, 0x157C, 0x1588, 0x1594, 0x15A0,
    0x15AC, 0x15B8, 0x15C4, 0x15D0, 0x15DC, 0x15EA, 0x15F6, 0x1602,
    0x160E, 0x161A, 0x1626, 0x1632, 0x163E, 0x164A, 0x1656, 0x1662,
    0x166E, 0x167A, 0x1686, 0x186C, 0x187B, 0x190F,
)

CASE_NAMES = (
    "single-p1", "single-p2", "tie", "reverse-tie", "nearest-secondary",
    "separate", "red", "pink", "green", "tie-replay",
)
VIEW_NAMES = ("standard", "wide")
EXECUTION_NAMES = ("native", "bounce")


def rom8(rom, address):
    if (address & 0xFFFF) < 0x8000:
        raise ValueError(f"invalid banked ROM address ${address:06X}")
    at = ((address >> 16) & 0x7F) * 0x8000 + (address & 0x7FFF)
    return rom[at]


def mouth_point(f, entity):
    """Match bank_02.asm CODE_02D149's mouth point from mount sprite fields."""
    r, slot = f.ram, entity[2]
    x = f.data[r + 0xE4 + slot] | (f.data[r + 0x14E0 + slot] << 8)
    y = f.data[r + 0xD8 + slot] | (f.data[r + 0x14D4 + slot] << 8)
    direction = f.data[r + 0x157C + slot]
    if direction not in (0, 1):
        raise ValueError(f"mount slot {slot} has invalid mouth direction {direction}")
    return ((x + (0x14 if direction == 0 else -4)) & 0xFFFF,
            (y + 8) & 0xFFFF)


def map16_location(f, rom, x, y, layer=0):
    """Match coop_terrain_block's stock horizontal/vertical Map16 addressing."""
    r = f.ram
    vertical = bool(f.data[r + 0x5B] & (1 << layer))
    screen = (y if vertical else x) >> 8
    limit = 14 if layer and vertical else 16 if layer else f.data[r + 0x5D]
    if screen >= limit or screen >= 32 or (x >= 512 if vertical else y >= 432):
        raise ValueError(f"mouth ({x},{y}) is outside layer-{layer} Map16 bounds")
    low_table = (0xBA8E if layer else 0xBA80) if vertical else (0xBA70 if layer else 0xBA60)
    high_table = (0xBACA if layer else 0xBABC) if vertical else (0xBAAC if layer else 0xBA9C)
    base = rom8(rom, low_table + screen) | (rom8(rom, high_table + screen) << 8)
    index = (y & 0xF0) | ((x & 0xFF) >> 4)
    index += (x if vertical else y) & 0xFF00
    at = base + index
    if not 0xC800 <= at <= 0xFFFF:
        raise ValueError(f"computed Map16 offset ${at:04X} is outside stock tile RAM")
    return at


def mount_owners(f):
    owners = {}
    for player, (_, record) in f.actors.items():
        identity = f.word(record + 52)
        if identity == 0xFFFFFFFF:
            continue
        entity = f.entities.get(identity)
        if not entity or entity[3] != 0x35 or entity[4] != player or entity[6] != 2:
            raise ValueError(f"player {player} has no validated ridden adult-Yoshi entity")
        owners[identity] = {"player": player, "entity": entity}
    if not owners:
        raise ValueError("source has no owned adult Yoshi")
    return owners


def owners_by_player(owners):
    return {info["player"]: info for info in owners.values()}


def mount_observations(f, owners):
    observations = {}
    for identity, info in owners.items():
        player = info["player"]
        raw = f.mounts.get(identity)
        if raw is None or len(raw) != len(MOUNT_FIELDS):
            observations[str(player)] = {"mount_id": identity, "state": "missing-mount-record"}
            continue
        values = dict(zip(MOUNT_FIELDS, raw))
        observations[str(player)] = {
            "mount_id": identity,
            "slot": info["entity"][2],
            "red_berries_eaten": values[0x18D4],
            "pink_berries_eaten": values[0x18D5],
            "eaten_berry_type": values[0x18D6],
            "tongue_timer": values[0x14A3],
            "eat_animation_timer": f.data[f.ram + 0x1564 + info["entity"][2]],
            "swallow_timer": values[0x18AC],
            "start_eat_timer": values[0x18AE],
            "egg_type": values[0x18DA],
            "egg_laid_timer": values[0x18DE],
        }
    return observations


def set_mount_position(f, info, x, y, direction=0):
    player, entity = info["player"], info["entity"]
    slot = entity[2]
    _, record = f.actors[player]
    power, reserve = f.word(record + 12), f.word(record + 16)
    life, recovery = f.word(record + 8), f.word(record + 44)
    half_width, height = f.word(record + 28), f.word(record + 32)
    # Native mounted coordinates are Mario's top-left at (Yoshi X, Yoshi Y-16).
    # coop_guest_read_player maps that to center (X+8, Yoshi Y+16), shape 2
    # (half-width 6, height 32). Keep the fixture's actual equipment/riding data.
    f.actor(player, x, y - 16, power, reserve, life, recovery)
    f.put(record + 20, x + 8)
    f.put(record + 24, y + 16)
    f.put(record + 28, half_width)
    f.put(record + 32, height)
    r = f.ram
    for low, high, value in ((0xE4, 0x14E0, x), (0xD8, 0x14D4, y)):
        f.data[r + low + slot] = value & 0xFF
        f.data[r + high + slot] = value >> 8
    f.data[r + 0x157C + slot] = direction


def swap_native_mount_slots(f, owners):
    if len(owners) != 2:
        raise ValueError("reversed-slot fixture needs exactly two ridden mounts")
    identities = list(owners)
    slots = [owners[i]["entity"][2] for i in identities]
    if slots[0] == slots[1]:
        raise ValueError("rider mounts already share a native sprite slot")
    r = f.ram
    for table in SPRITE_TABLES:
        a, b = r + table + slots[0], r + table + slots[1]
        f.data[a], f.data[b] = f.data[b], f.data[a]
    at = f.data.index(b"ENT1", f.core_end) + 16
    count = f.word(at - 12)
    for i in range(count):
        record = at + i * 28
        identity = f.word(record)
        if identity == identities[0]:
            f.put(record + 8, slots[1])
            entity = f.entities[identity]
            f.entities[identity] = entity[:2] + (slots[1],) + entity[3:]
        elif identity == identities[1]:
            f.put(record + 8, slots[0])
            entity = f.entities[identity]
            f.entities[identity] = entity[:2] + (slots[0],) + entity[3:]


def scenario(case, source):
    color = {"red": 0x45, "pink": 0x46, "green": 0x47}.get(case, 0x45)
    if case == "single-p1" or case in ("red", "pink", "green"):
        return {"riders": (0,), "positions": {}, "color": color,
                "buttons": "y", "slot_swap": False, "replay": False}
    if case == "single-p2":
        return {"riders": (1,), "positions": {0: (120, 352)}, "color": color,
                "buttons": "p2.y", "slot_swap": False, "replay": False}
    if case in ("tie", "tie-replay", "reverse-tie"):
        return {"riders": (0, 1), "positions": {}, "color": color,
                "buttons": "y+p2.y", "slot_swap": case == "reverse-tie",
                "replay": case == "tie-replay"}
    if case == "nearest-secondary":
        # Mouths land at x=58 (primary) and x=62 (secondary), both in the
        # same x=48..63 tile. Actor centers are x=46 and x=50, so secondary
        # is nearer the tile center x=56.
        return {"riders": (0, 1), "positions": {0: (38, 352), 1: (42, 352)},
                "color": color, "buttons": "y+p2.y", "slot_swap": False,
                "replay": False}
    if case == "separate":
        return {"riders": (0, 1), "positions": {0: (40, 352), 1: (80, 352)},
                "color": color, "buttons": "y+p2.y", "slot_swap": False,
                "replay": False}
    raise ValueError(f"unknown berry case: {case}")


def stage(source, output, rom, case):
    f = Fixture(source)
    if f.version < 5 or f.level != 0x106:
        raise ValueError("expected a version-5+ YI2 mount encounter state")
    owners = mount_owners(f)
    baseline_slots = {info["player"]: info["entity"][2] for info in owners.values()}
    spec = scenario(case, source)
    if not set(spec["riders"]).issubset({info["player"] for info in owners.values()}):
        raise ValueError(f"{case} source lacks the required mounted actors")
    if case in ("single-p1", "red", "pink", "green") and len(owners) != 1:
        raise ValueError(f"{case} requires a one-rider source fixture")
    if case not in ("single-p1", "red", "pink", "green") and len(owners) != 2:
        raise ValueError(f"{case} requires a two-rider source fixture")
    if spec["slot_swap"]:
        swap_native_mount_slots(f, owners)
        owners = mount_owners(f)
        if owners_by_player(owners)[0]["entity"][2] == baseline_slots[0]:
            raise ValueError("reverse-tie did not reverse player 0's native mount slot")
    for player, position in spec["positions"].items():
        info = next(info for info in owners.values() if info["player"] == player)
        set_mount_position(f, info, *position)
    for info in owners.values():
        start = mount_observations(f, {info["entity"][0]: info})[str(info["player"])]
        if any(start[key] != 0 for key in ("red_berries_eaten", "pink_berries_eaten",
                                           "eaten_berry_type", "tongue_timer",
                                           "swallow_timer", "start_eat_timer")):
            raise ValueError(f"source mount {info['player']} has nonempty berry/mouth state: {start}")

    timer_hundreds_before = f.data[f.ram + 0x0F31]
    timer_tens_before = f.data[f.ram + 0x0F32]

    points = {info["player"]: mouth_point(f, info["entity"])
              for info in owners.values() if info["player"] in spec["riders"]}
    offsets = {player: map16_location(f, rom, *point) for player, point in points.items()}
    if case in ("tie", "tie-replay", "reverse-tie", "nearest-secondary") and len(set(offsets.values())) != 1:
        raise ValueError(f"{case} mouths do not query the same tile: {points} => {offsets}")
    if case == "separate" and len(set(offsets.values())) != 2:
        raise ValueError(f"separate-berry mouths do not address different tiles: {points} => {offsets}")

    tile_records = {}
    for player, at in offsets.items():
        if at not in tile_records:
            before = f.data[f.ram + at] | (f.data[f.ram + at + 0x10000] << 8)
            tile_records[at] = {"players": [], "before": before}
        tile_records[at]["players"].append(player)
        f.data[f.ram + at] = spec["color"]
        f.data[f.ram + at + 0x10000] = 0
    f.save(output)
    return {
        "source": str(source.resolve()),
        "fixture": str(output.resolve()),
        "level": f.level,
        "case": case,
        "berry_map16_tile": spec["color"],
        "buttons": spec["buttons"],
        "slot_swap": spec["slot_swap"],
        "replay": spec["replay"],
        "timer_hundreds": timer_hundreds_before,
        "timer_tens": timer_tens_before,
        "timer_ones": f.data[f.ram + 0x0F33],
        "mounts": {str(info["player"]): {
            "id": identity,
            "slot": info["entity"][2],
            "mount_xy": [
                f.data[f.ram + 0xE4 + info["entity"][2]] + 256 * f.data[f.ram + 0x14E0 + info["entity"][2]],
                f.data[f.ram + 0xD8 + info["entity"][2]] + 256 * f.data[f.ram + 0x14D4 + info["entity"][2]],
            ],
            "mouth_xy": list(mouth_point(f, info["entity"])),
            "direction": f.data[f.ram + 0x157C + info["entity"][2]],
            "before_mouth_state": mount_observations(f, {identity: info})[str(info["player"])],
        } for identity, info in owners.items()},
        "tiles": {f"{at:04x}": {**data, "worldram_offsets": [at, at + 0x10000],
                                         "staged": spec["color"]}
                  for at, data in tile_records.items()},
    }


def inspect_state(path, expected_offsets):
    f = Fixture(path)
    owners = mount_owners(f)
    tile_by_offset = {
        str(at): f.data[f.ram + at] | (f.data[f.ram + at + 0x10000] << 8)
        for at in expected_offsets
    }
    return {
        "snapshot": str(path.resolve()),
        "tile_by_offset": tile_by_offset,
        "tile": next(iter(tile_by_offset.values())) if len(tile_by_offset) == 1 else None,
        "mounts": mount_observations(f, owners),
        "timer_hundreds": f.data[f.ram + 0x0F31],
        "timer_tens": f.data[f.ram + 0x0F32],
        "timer_ones": f.data[f.ram + 0x0F33],
    }


def timer_value(hundreds, tens, ones):
    return hundreds * 100 + tens * 10 + ones


def state_equal(left, right):
    return (left.data[left.ram:left.ram + 0x20000] == right.data[right.ram:right.ram + 0x20000]
            and left.data[left.machine:left.end] == right.data[right.machine:right.end])


def summarize(staged, save9, save11, tile_locations):
    initial = staged["mounts"]
    final = save11["mounts"]
    red_delta = {p: final.get(p, {}).get("red_berries_eaten", 0) -
                 initial[p]["before_mouth_state"]["red_berries_eaten"] for p in initial}
    pink_delta = {p: final.get(p, {}).get("pink_berries_eaten", 0) -
                  initial[p]["before_mouth_state"]["pink_berries_eaten"] for p in initial}
    timer_before = timer_value(staged["timer_hundreds"], staged["timer_tens"], staged["timer_ones"])
    timer_after = timer_value(save11["timer_hundreds"], save11["timer_tens"], save11["timer_ones"])
    return {
        "red_counter_delta_by_player": red_delta,
        "pink_counter_delta_by_player": pink_delta,
        "red_counter_delta_total": sum(red_delta.values()),
        "pink_counter_delta_total": sum(pink_delta.values()),
        "tiles_final": {f"{at:04x}": save11["tile_by_offset"][str(at)]
                        for at in tile_locations},
        "timer_tens_delta": save11["timer_tens"] - staged["timer_tens"],
        "timer_total_before": timer_before,
        "timer_total_after": timer_after,
        "timer_total_delta": timer_after - timer_before,
        "timer_hundreds_before": staged["timer_hundreds"],
        "timer_hundreds_after": save11["timer_hundreds"],
        "save9_mouths": save9["mounts"] if save9 else None,
        "save11_mouths": save11["mounts"],
    }


def run_case(root, name, source, exe, rom, case, view, execution, frames, control=False):
    label = f"{name}-control" if control else name
    fixture = root / f"{label}.sav"
    staged = stage(source, fixture, rom.read_bytes(), case)
    script = root / f"{label}.script"
    if staged["replay"]:
        script.write_text(
            f"wait 60\nloadstate 10\npress {staged['buttons']} 1\nwait 1\nsavestate 9\n"
            "wait 6\nsavestate 11\nloadstate 9\nwait 6\nsavestate 8\nwait 5\n",
            encoding="ascii", newline="\n")
        frames = 81
        capture = 70
    else:
        activity = "wait 40" if control else f"press {staged['buttons']} 40"
        script.write_text(f"wait 60\nloadstate 10\n{activity}\nsavestate 9\n"
                          "wait 120\nsavestate 11\nwait 5\n",
                          encoding="ascii", newline="\n")
        capture = 170
    out = root / label
    command = [
        sys.executable, str(ROOT / "tools/run_coop_probe.py"),
        "--exe", str(exe.resolve()), "--rom", str(rom.resolve()),
        "--snapshot", str(fixture.resolve()), "--script", str(script.resolve()),
        "--out", str(out.resolve()), "--frames", str(frames),
        "--framedump", str(capture), "--wram-watch", "18d4:18de",
        "--timeout-seconds", "300",
    ]
    if view == "standard":
        command.append("--native")
    if execution == "bounce":
        command.append("--bounce")
    try:
        completed = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                   timeout=330, check=False)
        wrapper = {"returncode": completed.returncode,
                   "stdout": completed.stdout, "stderr": completed.stderr}
    except subprocess.TimeoutExpired as exc:
        wrapper = {"returncode": 124, "timed_out_outer": True,
                   "stdout": (exc.stdout or b"").decode(errors="replace") if isinstance(exc.stdout, bytes) else exc.stdout,
                   "stderr": (exc.stderr or b"").decode(errors="replace") if isinstance(exc.stderr, bytes) else exc.stderr}
    (root / f"{label}-wrapper.json").write_text(json.dumps(wrapper, indent=2) + "\n")
    result_path = out / "result.json"
    result = json.loads(result_path.read_text()) if result_path.exists() else None
    observations = {"staged": staged, "view": view, "execution": execution,
                   "control_run": control,
                   "probe_command": command, "probe_result": result, "wrapper": wrapper}
    states = {}
    for slot in ((9, 11, 8) if staged["replay"] else (9, 11)):
        snapshot = out / "saves/native-coop-v1" / f"save{slot}.sav"
        if snapshot.exists():
            state = inspect_state(snapshot, [int(k, 16) for k in staged["tiles"]])
            observations[f"save{slot}"] = state
            states[slot] = Fixture(snapshot)
    if 9 in states and 11 in states:
        observations["summary"] = summarize(
            staged, observations["save9"], observations["save11"],
            [int(k, 16) for k in staged["tiles"]])
    if staged["replay"] and 11 in states and 8 in states:
        observations["replay"] = {
            "interval_frames": 6,
            "save11_equals_save8_wram_and_coop_payload": state_equal(states[11], states[8]),
        }
    log = out / "wram-writes.log"
    observations["wram_watch"] = str(log.resolve()) if log.exists() else None
    observations["wram_watch_bytes"] = log.stat().st_size if log.exists() else 0
    return observations


def expected_winner(case):
    return {"single-p1": "0", "single-p2": "1", "tie": "0",
            "reverse-tie": "0", "nearest-secondary": "1",
            "red": "0", "pink": "0", "green": "0"}.get(case)


def evaluate(case, observation, expectation):
    if expectation is None:
        return None
    if "harness_error" in observation or not observation.get("probe_result"):
        return {"passed": False, "reason": "fixture or probe did not produce observations"}
    result = observation["probe_result"]
    if result.get("returncode") != 0 or result.get("timed_out") or result.get("dispatch_misses"):
        return {"passed": False, "reason": "probe failed, timed out, or had dispatch misses"}
    summary = observation.get("summary", {})
    tile_consumed = bool(summary.get("tiles_final")) and all(
        tile == 0x49 for tile in summary["tiles_final"].values())
    if expectation == "duplicate":
        if case not in ("tie", "reverse-tie"):
            return None
        passed = summary.get("red_counter_delta_total") == 2 and tile_consumed
        return {"passed": passed,
                "reason": "expected two mount red-counter increments after one tile becomes Map16 $49"}
    if case in ("tie", "reverse-tie", "nearest-secondary", "single-p1", "single-p2"):
        winner = expected_winner(case)
        deltas = summary.get("red_counter_delta_by_player", {})
        passed = sum(deltas.values()) == 1 and deltas.get(winner) == 1 and tile_consumed
        return {"passed": passed, "expected_winner": int(winner),
                "reason": "one physical tile must award exactly once to the expected eligible actor"}
    if case == "separate":
        deltas = summary.get("red_counter_delta_by_player", {})
        passed = all(deltas.get(p) == 1 for p in ("0", "1")) and tile_consumed
        return {"passed": passed, "reason": "distinct berry tiles must award each eligible mount once"}
    if case == "red":
        passed = summary.get("red_counter_delta_total") == 1 and tile_consumed
        return {"passed": passed, "reason": "one red berry increments the red counter once"}
    if case == "pink":
        passed = summary.get("pink_counter_delta_total") == 1 and tile_consumed
        return {"passed": passed, "reason": "one pink berry increments the pink counter once"}
    if case == "green":
        control_observation = observation.get("timer_control", {})
        control_result = control_observation.get("probe_result")
        if (not control_result or control_result.get("returncode") != 0 or
                control_result.get("timed_out") or control_result.get("dispatch_misses")):
            return {"passed": False, "reason": "equal elapsed timer control did not complete cleanly"}
        control = control_observation.get("summary", {})
        reward_delta = summary.get("timer_total_after") - control.get("timer_total_after", 0)
        passed = reward_delta == 20 and tile_consumed
        return {"passed": passed, "timer_reward_over_equal_elapsed_control": reward_delta,
                "reason": "one green berry adds twenty timer units beyond the same elapsed no-input control"}
    if case == "tie-replay":
        replay = observation.get("replay", {})
        save9 = observation.get("save9", {})
        mouths = list(save9.get("mounts", {}).values())
        pending = any(m.get("eaten_berry_type", 0) for m in mouths)
        pending_tile = (len(save9.get("tile_by_offset", {})) == 1 and
                        next(iter(save9["tile_by_offset"].values())) == observation["staged"]["berry_map16_tile"])
        pending_counters = all(m.get("red_berries_eaten") == 0 and
                               m.get("pink_berries_eaten") == 0 for m in mouths)
        expected_type = observation["staged"]["berry_map16_tile"] - 0x44
        latched = [m.get("eaten_berry_type") for m in mouths
                   if m.get("eaten_berry_type") == expected_type]
        only_one_latch = (len(latched) == 1 and
                          sum(bool(m.get("eaten_berry_type")) for m in mouths) == 1)
        passed = (pending and pending_tile and pending_counters and only_one_latch and
                  replay.get("save11_equals_save8_wram_and_coop_payload") is True and tile_consumed)
        return {"passed": passed,
                "save9_contains_pending_mouth_state": pending,
                "save9_eat_animation_timers": [m.get("eat_animation_timer") for m in mouths],
                "save9_tile_is_still_berry": pending_tile,
                "save9_has_no_reward_yet": pending_counters,
                "save9_has_one_matching_eaten_type_latch": only_one_latch,
                "reason": "save9 must capture active mouth work; equal six-frame replay intervals must reproduce consumed berry and rewards"}
    return {"passed": False, "reason": "no expectation defined for case"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-single", type=Path,
                        default=ROOT / "build-adaptive/playtest/goal-20261019/mouth-ownership/acceptance-arbitration-first/encounter-0.sav")
    parser.add_argument("--source-two", type=Path,
                        default=ROOT / "build-adaptive/playtest/goal-20261019/mouth-ownership/acceptance-arbitration-first/encounter-1.sav")
    parser.add_argument("--exe", type=Path,
                        default=ROOT / "build-adaptive/SuperMarioWorldSNESRecomp.exe")
    parser.add_argument("--rom", type=Path, default=ROOT / "smw.sfc")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--frames", type=int, default=225)
    parser.add_argument("--cases", nargs="+", choices=CASE_NAMES,
                        default=("single-p1", "tie", "separate"))
    parser.add_argument("--views", nargs="+", choices=VIEW_NAMES, default=("standard",))
    parser.add_argument("--execution", nargs="+", choices=EXECUTION_NAMES, default=("native",))
    parser.add_argument("--expect", choices=("duplicate", "pass"))
    args = parser.parse_args()
    if args.frames < 225:
        parser.error("--frames must cover the scripted 225-frame bite/swallow observation")
    if args.expect == "duplicate" and not ({"tie", "reverse-tie"} & set(args.cases)):
        parser.error("--expect duplicate requires tie or reverse-tie in --cases")
    if args.expect == "pass" and not args.cases:
        parser.error("--expect pass requires at least one selected case")
    for path in (args.source_single, args.source_two, args.exe, args.rom):
        if not path.is_file():
            parser.error(f"missing input: {path}")
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=False)
    sources = {"single-p1": args.source_single, "red": args.source_single,
               "pink": args.source_single, "green": args.source_single,
               "single-p2": args.source_two, "tie": args.source_two,
               "reverse-tie": args.source_two, "nearest-secondary": args.source_two,
               "separate": args.source_two, "tie-replay": args.source_two}
    results = {}
    failures = 0
    for case in args.cases:
        for view in args.views:
            for execution in args.execution:
                name = f"{case}-{view}-{execution}"
                try:
                    observation = run_case(root, name, sources[case], args.exe, args.rom,
                                           case, view, execution, args.frames)
                    if case == "green":
                        observation["timer_control"] = run_case(
                            root, name, sources[case], args.exe, args.rom,
                            case, view, execution, args.frames, control=True)
                    expected = evaluate(case, observation, args.expect)
                    observation["expectation"] = expected
                    if observation.get("probe_result") and (
                            observation["probe_result"].get("returncode") != 0 or
                            observation["probe_result"].get("timed_out") or
                            observation["probe_result"].get("dispatch_misses")):
                        failures += 1
                    if expected and not expected["passed"]:
                        failures += 1
                except Exception as exc:
                    # Invalid fixture preconditions stay explicit failures.
                    observation = {"harness_error": f"{type(exc).__name__}: {exc}",
                                   "expectation": {"passed": False, "reason": "fixture/staging failed"}}
                    failures += 1
                results[name] = observation
    if set(args.execution) == {"native", "bounce"}:
        for case in args.cases:
            for view in args.views:
                native_name, bounce_name = f"{case}-{view}-native", f"{case}-{view}-bounce"
                native, bounce = results.get(native_name), results.get(bounce_name)
                native_path = native.get("save11", {}).get("snapshot") if native else None
                bounce_path = bounce.get("save11", {}).get("snapshot") if bounce else None
                equal = False
                if native_path and bounce_path:
                    equal = state_equal(Fixture(Path(native_path)), Fixture(Path(bounce_path)))
                comparison = {"compared": bool(native_path and bounce_path),
                              "full_wram_and_cnr_equal": equal,
                              "native_save11": native_path,
                              "bounce_save11": bounce_path}
                if native is not None:
                    native["native_bounce_comparison"] = comparison
                if bounce is not None:
                    bounce["native_bounce_comparison"] = comparison
                if not equal:
                    failures += 1
    (root / "observations.json").write_text(json.dumps({
        "classification": "berry acceptance evidence; only --expect enables behavior outcome gates",
        "behavioral_contract": "each real Map16 berry is consumed/rewarded at most once; distinct tiles remain independent",
        "native_tree_tile_after_GenerateTile_04": 0x49,
        "native_bounce_comparisons": "full 128 KiB WRAM and complete co-op CNR payload",
        "cases": results,
        "failures": failures,
    }, indent=2) + "\n")
    print(json.dumps({"out": str(root), "cases": list(results)}, indent=2))
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
