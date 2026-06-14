# MSU-1 patch — attribution & thanks

The MSU-1 audio support in this build does **not** originate with us. The
game-side driver — the code that detects the MSU-1 chip and streams music in
place of the SPC soundtrack — is homebrew work by others, and we credit them
plainly and gratefully.

## Which patch we use (important — see the multi-patch note below)

We bundle **Conn's "Super Mario World MSU-1"** patch — the *audio-only / native
music-replacement* patch. It hooks the music engine and streams `.pcm` tracks
when an MSU-1 device + pack is present, and replays the original SPC music when
it isn't. It makes **no gameplay changes** — which is exactly what we want for a
faithful recompile.

- `smw_msu.ips` — the patch we apply (600 bytes; injects a ~520-byte driver
  into bank `$04` freespace at `$04:EF46` and 5 JSL hooks in banks `$00`/`$04`).
- `smw_msu1.asm` — the annotated driver source (Conn, 2015). Header credits
  Conn, with thanks to Ikari_01, EmuandCo, Kiddo, et al.
- `manifest.bml` — the bsnes/higan board manifest that ships with the patch.

## Authors

- **Conn** — wrote the Super Mario World MSU-1 driver (`smw_msu1.asm`) that this
  patch installs.
- **Ikari_01, EmuandCo, Kiddo** and the wider SMW Central / Zeldix MSU-1
  community — thanked in the driver source for the groundwork.

Patch home: <https://www.zeldix.net/t1436-super-mario-world-native>

## ⚠ THERE ARE THREE DIFFERENT SMW MSU-1 PATCHES — PCM PACKS ARE NOT INTERCHANGEABLE

This is a real footgun. Super Mario World has **three** distinct MSU-1 patches,
each with its **own** PCM track set. A pack built for one patch will play the
**wrong tracks** (or silence) under another, because the track-number → file
mapping differs. We ship and recompile from **only the first one**:

| Patch | Scope | We use it? | Pack source |
|---|---|---|---|
| **SMW MSU-1** (Conn, audio-only) | music replacement, no gameplay changes | ✅ **yes** | <https://www.zeldix.net/t1436-super-mario-world-native> |
| SMW MSU+ | enhancement hack (gameplay/feature changes) | ❌ no — not faithful | <https://www.zeldix.net/t1437-super-mario-world-msu> |
| SMW MSU-1 Plus Ultra (130 tracks) | large enhancement hack | ❌ no | <https://www.zeldix.net/t2535-super-mario-world-msu-1-plus-ultra-130-tracks-total> |

**Use a PCM pack built for "SMW MSU-1" (the t1436 audio-only patch).** Packs for
MSU+ or Plus Ultra will not line up with this build.

## License / what we redistribute

The IPS patch is a diff of *new* bytes (the homebrew driver, the hook
instructions, padding) — it contains **no original Nintendo ROM data**. Our
regen step (`tools/apply_msu_patch.py`, run by `tools/regen.sh`) applies this
patch to *your own* legally-obtained stock SMW (USA) ROM in a throwaway file and
recompiles from the result. You supply the ROM; we supply only the freely-shared
homebrew patch and the recompiler. The recompiled build then runs on the stock
ROM directly — no pack → authentic SPC audio; matching pack + MSU-1 enabled →
streamed music.

**Thank you, Conn and the MSU-1 community.** If you enjoy the streamed music
here, the credit is theirs.
