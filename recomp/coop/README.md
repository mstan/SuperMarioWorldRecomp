# Simultaneous co-op patch

`smw_coop.ips` is a ROM-data-free delta generated from the user-supplied
`super_mario_world_co-op_hack.rar`. The archive itself and its patched ROM are
not distributed by this project.

Patch provenance:

- Input: canonical unheadered Super Mario World (USA), 524,288 bytes
- Input SHA-256: `0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b`
- Output: headerless simultaneous co-op ROM, 1,048,576 bytes
- Output SHA-256: `054af32f70b9955a508e8603b5448f654eb9915c251612f7d1edbdfe6cf15ea1`
- Output CRC32: `05D84AE1`

At runtime, the co-op executable verifies a stock ROM, applies this IPS beside
the executable as `<stock-name>.coop.sfc`, and loads that generated file. The
developer regen pipeline applies this patch directly to the canonical stock ROM
so its analysis input is byte-for-byte identical to the runtime-generated ROM.

Regenerate the IPS only from the exact local inputs:

```text
python tools/create_ips.py --source smw.sfc \
  --target "Super Mario World Co-op Hack.smc" \
  --out recomp/coop/smw_coop.ips \
  --expect-source-sha256 0838e531fe22c077528febe14cb3ff7c492f1f5fa8de354192bdff7137c27f5b \
  --expect-target-sha256 054af32f70b9955a508e8603b5448f654eb9915c251612f7d1edbdfe6cf15ea1
```
