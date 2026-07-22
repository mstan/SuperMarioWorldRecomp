#!/usr/bin/env python3
"""Apply the optional override layer to freshly generated banks.

Runs at build time, AFTER snesrecomp emits src/gen/ and BEFORE compilation.
For each rule in a manifest it injects an idempotent, runtime-gated dispatch
prologue into the matching generated function(s), so the override layer
survives regeneration without anyone hand-editing src/gen/.

See overrides/README.md for the full design and the contract override bodies
must follow.

Usage:
    python tools/apply_overrides.py [--gen-dir src/gen]
        [--manifest overrides/widescreen/overrides.manifest] [--check] [-v]
    python tools/apply_overrides.py --restore [--gen-dir src/gen] [-v]

With no manifest rules active this is a no-op (authentic build). Safe to run
on every build: injection is marked and skipped if already present.

--restore is the exact inverse: every injected ` /*WS-...*/ { ... }` snippet
is located by its marker and removed up to its balanced closing brace,
returning the gen files to the pristine regen output. Used by
tools/make_release.ps1 to build the standard (authentic) zip from the same
worktree that builds the widescreen zip. Idempotent; after a restore the gen
dir contains zero WS markers (verifiable via grep).
"""
import argparse
import os
import re
import sys

MARKER = "/*WS-OVERRIDE*/"

# --- Block-level widescreen patches (runtime-gated, default-off) ---------
# Some widescreen behaviour can't be a whole-function override (the recompiled
# functions carry CpuState/NLR plumbing). Instead we inject a small gated
# snippet right after a specific recompiled basic-block anchor. Each patch is
# anchored on a unique generated line so it targets exactly one site per
# (m,x) variant, and is idempotent via its marker.
#
# WS-FLAG: widen GetDrawInfo's draw/cull window ONLY. Vanilla GetDrawInfo
# (banks 01/02/03) culls a sprite when screen-x is outside [-64, 320)
# (tx = sprX + 0x40 - camX; cull when tx >= 0x180): it stores the cull bool to
# spr_table15c4 ($15C4) and double-returns so the GFX routine never draws.
# For widescreen we widen that window to [-(64+g_ws_extra), 256+g_ws_extra):
# 64 = vanilla's pad for the widest sprites straddling the left edge, mirrored
# at the widescreen edge; the right bound needs no pad (tiles extend rightward
# from the origin and the presentation crops them).
#
# CRITICAL — do NOT touch spr_xoffscreen ($15A0). It is set earlier in
# GetDrawInfo to "screen-x not in [0,256)" and it IS the OAM x-high (9th
# position bit) source: generic draw routines emit
# `sprites_oamtile_size_buffer[slot+64] = $15A0|2` and FinishOAMWrite
# recomputes the same predicate from world coords. With vanilla $15A0, margin
# sprites get x-high=1 and the PPU's widened sprite-x wrap threshold
# (256+g_ws_extra) presents 9-bit x >= threshold as negative (left margin).
# A previous revision of this patch forced $15A0=0 inside the widescreen
# window; that zeroed the x-high bit and made every left-margin sprite
# (incl. Yoshi) render 256px to the RIGHT — the classic 9-bit wrap teleport.
#
# 9-bit representability bounds the widening: left tiles live at 512-(64+extra)
# .. 511 and must stay >= the wrap threshold 256+extra, so extra < 96. The
# snippet clamps. No-op when widescreen is off.
#
# ALL game-logic snippets are additionally gated on game mode $0100 == 0x14
# (player-controlled level main), mirroring the presentation's pillarbox
# gate in main.c. The title attract demo runs the level engine under game
# mode 0x07 with scripted inputs; widened spawn timing changes sprite slot
# allocation and desyncs the recorded choreography (user-observed: Yoshi's
# stomp missing the sliding koopa). Under mode 0x07 the presentation is
# pillarboxed anyway (PpuSetExtraSpaceCentered zeroes the PPU margins), so
# vanilla simulation + vanilla view stay paired.
#
# WS-DESPAWN: widen SubOffscreen's horizontal erase bounds by g_ws_extra.
# Each bank (01/02/03) carries its own copy of SubOffscreen with its own
# bounds tables (contents differ per bank!). The horizontal decision joins
# at one block per bank: `LDA $00 : BPL keep / fall into EraseSprite`,
# where $00 = hibyte(bound[r1] + camera - sprX), sign-flipped for odd r1
# (left bounds), and Y still holds r1. The snippet re-derives the full
# 16-bit comparison from the bank's own table (read via DB like the
# original) and rewrites $00 with the widened verdict (0x80 = erase,
# 0x00 = keep) just before the vanilla load tests it. Even r1 = right
# bound (erase if sprX > cam+bound+extra), odd r1 = left bound (erase if
# sprX < cam+bound-extra). extra=0 reduces exactly to vanilla. The
# offscreen GATE ($15A0|$186C) stays vanilla — it is also the x-high bit
# (see WS-FLAG) and only opens the despawn check, never forces it.
#
# Left bounds get a cushion, not just a shift: vanilla left bounds encode
# each class's width slack against a screen edge at 0 (Entry1 -0x40,
# Entry3 only -0x10, bank03 even +0x40). A uniform -extra shift keeps
# that slack, but the widescreen edge is at -extra and a 16-32px sprite
# of a shallow-bound class still blinks out visibly inside the margin
# (user-observed: left-side despawn premature, right side perfect). So
# every left bound is clamped to at least -(64+extra) — the same
# widest-sprite cushion as the WS-FLAG draw window — while deeper vanilla
# bounds (e.g. -0x70) keep their extra slack. Right bounds are origin-
# based with the body extending away from view; the plain shift is
# already invisible there. Hysteresis: spawn column -0x30-extra stays
# inside the shallowest left despawn -(64+extra).
#
# WS-CHAIN: the brown chained platform ($5F, BrownChainedPlat) is the ONE
# sprite in the ROM that draws its OAM without GetDrawInfo (audit: exactly
# four $15C4 SpriteWayOffscreenX stores exist — banks 01/02/03 GetDrawInfo,
# all WS-FLAG-covered, plus the platform's private store at $01C9EC). Two
# widescreen defects follow:
#
# 1. Interaction window: $01C9EC compares the DRAWN platform screen-x
#    (BrSwingPlatXPos $14B8 - camera, i.e. up to 0x50+0x28 px away from the
#    sprite's nominal x) against a hardcoded [-0x10, 0x110) window and
#    early-returns past the Mario-contact check when outside. In widescreen
#    a platform visible in either margin is non-solid and flagged
#    way-offscreen. Widen to [-(0x10+extra), 0x110+extra).
#
# 2. 9-bit alias hole: the routine writes raw 8-bit tile x; presentation
#    reconstructs sign via the x-high bit against the widened wrap
#    threshold 256+extra, so the representable tile window is
#    [-(256-extra), 255+extra) mod 512. Vanilla's threshold 256 covers
#    [-256,255] exactly (no hole), and vanilla geometry keeps this
#    sprite's tiles >= -160 (despawn at nominal -64, frozen-pose graphic
#    -0x50-0x28 further left). The WIDENED despawn (-(64+extra)) keeps it
#    alive with tiles near -(64+extra+0xA8) ~ -215 < -185: those encode as
#    9-bit 256..326 and present INSIDE the visible right margin — the
#    user-visible "second platform pops in at the top-right and vanishes"
#    (it is the frozen LEFT neighbor, wrapped). Mirror case: a swinging
#    platform far right (tiles >= 512-extra) presents in the left margin
#    ("phantom flicker"). Fix: after the vanilla store, if any of the
#    sprite's tiles could encode into a visible margin from the far side,
#    park all 10 of its OAM tiles (y=0xF0) for this frame. The hide can
#    never blank a partially-visible platform: the alias zones and the
#    visible margins are >120px apart and the sprite spans <=160px.
#
# Generic sprites stay safe without this: GetDrawInfo's widened cull
# (WS-FLAG) guarantees origin >= -(64+extra), and no generic SMW sprite
# draws tiles more than (256-extra)-(64+extra) = 192-2*extra (= 50px at
# extra=71) left of its origin. The chained platform (0xA8 left) is the
# sole outlier — hence this patch completes the class.
#
# 3. OAM-region wrap (the WS-SLOT consequence): the platform draws 10
#    tiles with an 8-BIT OAM index (INY x4 from SpriteOAMIndex). The
#    two vanilla reserved slots get 10-tile regions (bases 0x30/0x58
#    rel $0300), but WS-SLOT's third slot (slot 5) keeps the normal
#    5-tile region at base 0xE4 — the last 3 tiles (the three LEFT
#    platform log tiles, written at wrapped Y=0x00/0x04/0x08) land in
#    $0300-$030B, the PLAYER's reserved OAM region. PlayerDraw sweeps
#    attr bytes there each frame with the player page (0x60); the
#    platform's log tile $60 on page 0 IS the smoke-puff tile, so the
#    last-writer race renders a CLOUD at the leftmost log tile
#    whenever Mario rides (riding makes the platform re-call
#    DrawMarioAndYoshi after its tile writes, flipping the race).
#    OAM is fully allocated under header $01 — no contiguous 40-byte
#    region exists, and all three platforms can have visible pixels
#    simultaneously in widescreen (span 432px vs 398px view), so the
#    third platform cannot simply hide. Fix: the WS-RELOC patch below
#    relocates the wrapped tile that lands on an attr-swept player
#    entry and parks the stomped bytes, every frame, after the draw.
#    The alias-park (defect 2) moved into the same WS-RELOC loop: it
#    now runs on SpriteLock frames too (the old host block $01C9EC is
#    skipped under lock while the draw is not), and it parks at
#    wrap-aware addresses — the previous unwrapped park loop wrote y
#    bytes into the PACKED OAM high table at $0400-$041F for slot-5
#    platforms. WS-CHAIN itself keeps only the interaction-window
#    widening and flag override at the $15C4 store.
#
# WS-SLOT: give the chained platform ($5F) a third reserved sprite slot
# in widescreen. Sprite-memory settings with ReservedSprite1 == $5F
# (header $01/$11) confine $5F to a reserved slot range; the allocation
# loop at $02A918 scans X = SpriteSlotMax1 (7) down to EXCLUSIVE floor
# _6 = SpriteSlotStart1 (5), i.e. slots {6,7} only — two simultaneous
# $5F. Levels are authored to that budget against VANILLA despawn
# windows: in the level-$097 platform row, vanilla's shallow $5F left
# bound (-0x10) erases the previous platform exactly as the next one's
# spawn column enters the parse sweep, time-multiplexing the two slots.
# WS-DESPAWN's left cushion (-(64+extra)) keeps the previous platform
# alive through the whole camera swing, permanently starving the next
# platform's spawn (allocation fails, load-status is cleared, the
# column is re-swept but the slots are never free) — user-visible as
# "the right grey block has no platform attached". Re-tightening the
# bounds would bring back visible blink-out, so instead widen the slot
# budget: when allocating a $5F and the reserved floor is 5, lower the
# floor to 4 so the loop also tries slot 5 — three platforms coexist.
# Slot 5 is the top of the normal-sprite range (normals fill 5 down to
# 0), so it is only contended when five+ normal sprites are live.
# Gated like every WS patch; vanilla allocation is untouched when off.
#
# WS-WING: the para-koopa wing tile (KoopaWingGfxRt, $019E28) is drawn
# AFTER its body's FinishOAMWrite pass and carries its own hard cull:
# after storing the wing's 8-bit screen-x it tests the 16-bit high byte
# of (wing world-x - camera) and skips y/tile/attr/size unless the wing
# is in [0, 256) — a private 4:3 window. In widescreen the koopa body
# (GetDrawInfo + WS-FLAG) renders across the full view while its wings
# pop in/out at the vanilla screen edge (user-observed on the green
# bouncing paratroopa). The other wing sub-draw, GoombaWingGfxRt, needs
# NO patch: it ends with its own FinishOAMWriteRt call (A=#$02,Y=#$FF)
# which recomputes per-tile x-high from world coords — already correct
# in the margins.
#
# Fix: at the entry of the wing draw block ($019E3C), when the wing's
# true screen-x lies in a visible margin ([-extra, 0) or
# [256, 256+extra)) — where vanilla will skip — write the y/tile/attr
# ourselves (same 8-bit math and ROM constants as the vanilla path:
# disp tables at $019E10-$019E27) and the size byte with the x-high
# bit SET (vanilla never sets it because in its [0,256) window x-high
# is always 0; in the margins the 9-bit reconstruction against the
# widened wrap threshold needs bit 8 = 1 on both sides). Vanilla then
# stores the x low byte itself (that store is unconditional) and its
# own branch skips the rest, so there is no double-write; the
# in-window path is untouched.
#
# WS-SPAWN: widen ParseLevelSpriteList's spawn trigger so sprites spawn
# beyond the visible widescreen margin instead of materializing inside
# it. Vanilla parses ONE column per run (every other frame): camera-0x30
# scrolling left / camera+0x120 scrolling right ($55 = 0/2; $5B bit0 =
# vertical level, left untouched — widescreen is horizontal-only). At
# the join block $02A828 the column low byte (&0xF0) is in $00 and the
# high byte is in A; the snippet recomputes both.
#
# A plain outward shift of the single column is NOT enough: the parser
# is a moving sweep EDGE, and shifting it leaves the strip between the
# vanilla edge and the widened edge permanently unswept. A sprite that
# despawns while its column sits in that strip can never respawn unless
# the camera retreats past it — observed with the $5F chain-platform
# row in level $097: riding a platform swings the camera in a ~136px
# oscillation; the next platform's column (cam+165..+311) lies inside
# the strip forever, so after one early erase the platform is gone for
# good ("the right grey block has no platform"). Vanilla's unshifted
# edge re-crosses such columns every camera oscillation and re-spawns
# them (load-status $1938 dedups sprites that are still alive).
#
# So the snippet alternates per parse run (run index = TrueFrame>>1,
# the routine only runs on even frames):
#   odd runs  — LEAD: the widened edge (vanilla offset + extra), same
#               as before; fires every 4 frames, covering sustained
#               scroll up to 4px/frame (> SMW's max run speed; only
#               turn-around camera snaps exceed it, where vanilla
#               itself pops sprites at the screen edge).
#   even runs — RECOVERY: a column rotating in 16px steps across
#               [vanilla edge .. vanilla edge + extra], restoring
#               vanilla's full sweep coverage so stranded-but-
#               respawnable sprites come back. Re-parses of loaded
#               sprites are no-ops via $1938.
# All swept columns stay inside the WS-DESPAWN-widened erase bounds
# (checked per bounds class, including $5F's +0x78 init displacement,
# the tightest case: max recovery column +0x120+0x40+15 = cam+0x167 ==
# its widened erase threshold) so sweep-spawn never insta-erases.
# Hysteresis vs WS-DESPAWN is preserved: both sides move by the same
# extra (vanilla gap: spawn +0x120 < erase +0x130; spawn -0x30 > erase
# -0x40). If the widened column would precede the level start (<0) the
# vanilla column is kept.
#
# WS-COOP-TILE: the simultaneous-co-op IPS redirects the normal level
# tilemap update at $05:86F7 to its four-camera-aware replacement at
# $1F:B206. Its Layer 1 column builder ($1F:AA66) uses a much tighter
# look-ahead table at $1F:B15F: {-1, +17} Map16 columns, versus vanilla's
# {-8, +23}. +17 is just one column beyond the authentic 256-pixel view;
# after the centered widescreen view advances, the next VRAM page therefore
# still contains the hack's $25 clear tile and appears as a patterned slab.
#
# The first patch calls the shared priming helper at the replacement update
# routine's entry. It makes the current cursor stale for just enough opening
# frames to fill the added columns contiguously. The second patch adjusts the
# direction-table value already loaded into local `_v11`; the shared helper
# returns successive prime offsets, then the full live margin. This keeps
# Standard byte-for-byte authentic, changes only the co-op Layer 1 builder,
# and streams real Map16 columns rather than synthesizing terrain in the PPU.


def _ws_despawn_patch(anchor_pc, tbl_lo):
    """Per-bank WS-DESPAWN entry. anchor_pc = the `LDA $00` join block;
    tbl_lo = that bank's 8-entry bounds table (hi bytes at tbl_lo+8)."""
    return {
        "marker": "/*WS-DESPAWN*/",
        # Anchor on the unique trace line of the join block; every
        # emitted copy of that block (entry funcs x (m,x) variants) is
        # the despawn body, so no func name scoping is needed.
        "func_match": "",
        "anchor": "cpu_trace_block(cpu, 0x%06X)" % anchor_pc,
        "snippet": (
            " /*WS-DESPAWN*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14) {"
            " unsigned int _wk = cpu->X & 0xffffu; unsigned int _wy = cpu->Y & 7u;"
            " int _wb = (int)(short)(unsigned short)("
            "cpu_read8(cpu,cpu->DB,(unsigned short)(0x%04Xu+_wy))"
            " | (cpu_read8(cpu,cpu->DB,(unsigned short)(0x%04Xu+_wy))<<8));"
            " int _wv = (int)(short)(unsigned short)((unsigned int)(_wb"
            " + (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8))"
            " - (cpu_read8(cpu,0x7E,(unsigned short)(0x00E4+_wk))"
            " | (cpu_read8(cpu,0x7E,(unsigned short)(0x14E0+_wk))<<8))) & 0xFFFFu);"
            " int _wthr = (_wb <= -64) ? g_ws_extra : (_wb + 64 + g_ws_extra);"
            " int _werase = (_wy & 1) ? (_wv >= _wthr) : (_wv < -g_ws_extra);"
            " cpu_write8(cpu,0x7E,(unsigned short)(cpu->D + 0x0000),"
            " (unsigned char)(_werase ? 0x80 : 0x00)); } }"
        ) % (tbl_lo, tbl_lo + 8),
    }


BLOCK_PATCHES = [
    {
        "marker": "/*WS-COOP-TILE*/",
        "func_match": "bank_1F_B206_M0X0",
        "anchor": "cpu_trace_block(cpu, 0x1FB206)",
        "snippet": (
            " /*WS-COOP-TILE*/ {"
            " extern void SmwCoopWidescreenTileBegin(CpuState *cpu);"
            " SmwCoopWidescreenTileBegin(cpu); }"
        ),
    },
    {
        "marker": "/*WS-COOP-TILE*/",
        "func_match": "bank_1F_AA66_M1X1",
        "anchor": "uint16 _v11 = cpu_read16(cpu, (uint8)((((uint32)0x1fb15f + (uint32)cpu->X)) >> 16), (uint16)(((uint32)0x1fb15f + (uint32)cpu->X)));",
        "snippet": (
            " /*WS-COOP-TILE*/ {"
            " extern int SmwCoopWidescreenTileOffset(CpuState *cpu);"
            " _v11 = (uint16)(_v11 + SmwCoopWidescreenTileOffset(cpu)); }"
        ),
    },
    {
        "marker": "/*WS-COOP-ROW*/",
        "func_match": "bank_1F_AB83_M1X1",
        "anchor": "cpu_trace_block(cpu, 0x1FAB83)",
        "snippet": (
            " /*WS-COOP-ROW*/ {"
            " extern void SmwCoopWidescreenRowBegin(CpuState *cpu);"
            " SmwCoopWidescreenRowBegin(cpu); }"
        ),
    },
    {
        "marker": "/*WS-COOP-ROW*/",
        "func_match": "bank_1F_AB83_M1X1",
        "anchor": "cpu_trace_block(cpu, 0x1FAC23)",
        "snippet": (
            " /*WS-COOP-ROW*/ {"
            " extern void SmwCoopWidescreenRowEnd(CpuState *cpu);"
            " SmwCoopWidescreenRowEnd(cpu); }"
        ),
    },
    {
        "marker": "/*WS-COOP-ROW*/",
        "func_match": "bank_1F_AC27_M1X1",
        "anchor": "cpu_trace_block(cpu, 0x1FAC27)",
        "snippet": (
            " /*WS-COOP-ROW*/ {"
            " extern void SmwCoopWidescreenRowBegin(CpuState *cpu);"
            " SmwCoopWidescreenRowBegin(cpu); }"
        ),
    },
    {
        "marker": "/*WS-COOP-ROW*/",
        "func_match": "bank_1F_AC27_M1X1",
        "anchor": "cpu_trace_block(cpu, 0x1FAC90)",
        "snippet": (
            " /*WS-COOP-ROW*/ {"
            " extern void SmwCoopWidescreenRowEnd(CpuState *cpu);"
            " SmwCoopWidescreenRowEnd(cpu); }"
        ),
    },
    {
        "marker": "/*WS-FLAG*/",
        # Only inside GetDrawInfo* (banks 01/02/03; normal sprites use the
        # bank-01 copy). Anchor on the spr_table15c4 ($15C4) write — the
        # draw-cull store, present once per variant, immediately before the
        # branch `if (_flag_Z == 0) goto <off-screen double-return>`.
        "func_match": "GetDrawInfo",
        "anchor": "0x15c4 + (uint32)cpu->X",
        # Recompute the draw decision against the widened window and override
        # the branch flags (_flag_Z: Z=1 means draw, mirroring vanilla's
        # `AND #1` result of 0) plus the just-stored $15C4 cull bool. A is dead
        # past this point on both paths (reloaded immediately); $15A0 is left
        # at its vanilla value on purpose (see header comment).
        "snippet": (
            " /*WS-FLAG*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14) {"
            " unsigned int _wk = cpu->X & 0xffffu;"
            " int _we = g_ws_extra > 95 ? 95 : g_ws_extra;"
            " int _wsx = (int)(short)("
            "(cpu_read8(cpu,0x7E,(unsigned short)(0x00E4+_wk))"
            " | (cpu_read8(cpu,0x7E,(unsigned short)(0x14E0+_wk))<<8))"
            " - (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8)) );"
            " int _wdraw = (_wsx >= -(64 + _we) && _wsx < 256 + _we);"
            " cpu->_flag_Z = _wdraw ? 1 : 0; cpu->_flag_C = _wdraw ? 0 : 1;"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x15C4+_wk), _wdraw ? 0 : 1); } }"
        ),
    },
    {
        "marker": "/*WS-CHAIN*/",
        # The brown chained platform's private way-offscreen store at
        # $01C9EC (see header comment). The $15C4 store line also appears
        # in GetDrawInfo bodies, so scope to the one recompiled function
        # that holds block $01C9EC. Appending to the store line places the
        # snippet after the vanilla store and before the early-return
        # branch on _flag_Z, so both can be overridden.
        "func_match": "sub_1C9EC",
        "anchor": "0x15c4 + (uint32)cpu->X",
        "snippet": (
            " /*WS-CHAIN*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14) {"
            " int _we = g_ws_extra > 95 ? 95 : g_ws_extra;"
            " int _wcam = cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8);"
            " int _wplat = (int)(short)(unsigned short)((unsigned int)("
            "(cpu_read8(cpu,0x7E,0x14B8) | (cpu_read8(cpu,0x7E,0x14B9)<<8)) - _wcam) & 0xFFFFu);"
            " int _wctr = (int)(short)(unsigned short)((unsigned int)("
            "(cpu_read8(cpu,0x7E,0x14B0) | (cpu_read8(cpu,0x7E,0x14B1)<<8)) - _wcam) & 0xFFFFu);"
            " int _wkeep = (_wplat >= -(0x10 + _we)) && (_wplat < 0x110 + _we);"
            " unsigned char _wval = (unsigned char)((_wkeep ? 0 : 1)"
            " | cpu_read8(cpu,0x7E,(unsigned short)(cpu->D + 0x009d)));"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x15C4 + (cpu->X & 0xffffu)), _wval);"
            " cpu_write_a_m(cpu, (uint16)_wval);"
            " cpu->_flag_Z = (_wval == 0) ? 1 : 0; cpu->_flag_N = 0; } }"
        ),
    },
    {
        "marker": "/*WS-RELOC*/",
        # The chained platform's post-draw block (see WS-CHAIN header
        # comment, defects 2 and 3): $01C9A1 = `LDX CurSpriteProcess`
        # right after the 10-tile xhigh/cull loop, runs every frame the
        # platform is alive INCLUDING SpriteLock frames (message boxes,
        # death animation) where the $01C9EC interaction block — the old
        # park-loop host — is skipped. Unscoped like WS-DESPAWN: every
        # emitted copy of the block is the platform draw body.
        # Registers are not yet repurposed here, so everything is read
        # from WRAM ($15E9 = sprite slot).
        #
        # Two jobs, one loop over the 10 logical tiles:
        # - alias-park (moved from WS-CHAIN): when any tile could 9-bit
        #   alias into a visible margin from the far side, park all 10
        #   (at their FINAL locations, scraps included).
        # - wrap relocation: tiles whose byte index passed 0xFF landed in
        #   the sprite OAM region's first entries. Targets 0x00/0x04
        #   (entries 64/65) are free in mode-0x14 gameplay (PlayerDraw's
        #   attr sweep covers entries 66-72 + 126 only — verified via
        #   the WRAM-write ring), so those tiles stay put (_wd == _wo).
        #   The leftmost log tile (entry 66, byte 0x08) IS attr-swept —
        #   the user-visible cloud — and relocates to entry 73 (byte
        #   0x24, between the player set and reserved slots 10/11; its
        #   extras byte $0469 is equally unowned). Sources are parked
        #   after copying so no platform garbage stays where PlayerDraw
        #   expects to own the bytes. Stale-scrap safety: while the
        #   wrapping platform is alive it rewrites or parks the scraps
        #   every frame; it can only stop (despawn) out of view, where
        #   the scraps' last screen-relative x is off-margin forever.
        "func_match": "",
        "anchor": "cpu_trace_block(cpu, 0x01C9A1)",
        "snippet": (
            " /*WS-RELOC*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14) {"
            " int _we = g_ws_extra > 95 ? 95 : g_ws_extra;"
            " int _wcam = cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8);"
            " int _wplat = (int)(short)(unsigned short)((unsigned int)("
            "(cpu_read8(cpu,0x7E,0x14B8) | (cpu_read8(cpu,0x7E,0x14B9)<<8)) - _wcam) & 0xFFFFu);"
            " int _wctr = (int)(short)(unsigned short)((unsigned int)("
            "(cpu_read8(cpu,0x7E,0x14B0) | (cpu_read8(cpu,0x7E,0x14B1)<<8)) - _wcam) & 0xFFFFu);"
            " int _wlo = (_wplat < _wctr ? _wplat : _wctr) - 0x28;"
            " int _whi = (_wplat > _wctr ? _wplat : _wctr) + 0x18 + 15;"
            " int _walias = (_wlo < _we - 256 || _whi >= 512 - _we);"
            " unsigned int _wspr = cpu_read8(cpu,0x7E,0x15E9);"
            " unsigned int _woi = cpu_read8(cpu,0x7E,(unsigned short)(0x15EA + _wspr));"
            " static const unsigned char _wsc[3] = {0x00, 0x04, 0x24};"
            " int _wj = 0;"
            " for (int _wk = 0; _wk < 10; _wk++) {"
            " unsigned int _wraw = _woi + (unsigned int)_wk*4u;"
            " unsigned int _wo = _wraw & 0xFFu;"
            " if (_wraw > 0xFFu) {"
            " unsigned int _wd = (_wj < 3) ? _wsc[_wj++] : _wo;"
            " if (_walias) {"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0301u + _wd), 0xF0);"
            " if (_wd != _wo) cpu_write8(cpu,0x7E,(unsigned short)(0x0301u + _wo), 0xF0);"
            " } else if (_wd != _wo) {"
            " for (int _wb = 0; _wb < 4; _wb++)"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0300u + _wd + (unsigned int)_wb),"
            " cpu_read8(cpu,0x7E,(unsigned short)(0x0300u + _wo + (unsigned int)_wb)));"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0460u + (_wd >> 2)),"
            " cpu_read8(cpu,0x7E,(unsigned short)(0x0460u + (_wo >> 2))));"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0301u + _wo), 0xF0); }"
            " } else if (_walias) {"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0301u + _wo), 0xF0); } } } }"
        ),
    },
    # WS-DESPAWN (see header comment): one entry per bank copy.
    _ws_despawn_patch(0x01AC7C, 0xAC11),  # SubOffscreen_Bank01_* join + tables
    _ws_despawn_patch(0x02D076, 0xD007),  # SubOffscreen_Bank02_* join + tables
    _ws_despawn_patch(0x03B8A8, 0xB83F),  # SubOffscreen_Bank03_* join + tables
    {
        "marker": "/*WS-SLOT*/",
        # All copies of the allocation join block $02A916 (Parse entries
        # + the LoadShooter tail copy); the $5F sprite-number condition
        # self-scopes, shooters are never $5F.
        "func_match": "",
        "anchor": "cpu_trace_block(cpu, 0x02A916)",
        "snippet": (
            " /*WS-SLOT*/ { extern bool g_ws_active;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14"
            " && cpu_read8(cpu,0x7E,(unsigned short)(cpu->D + 0x0005)) == 0x5F"
            " && cpu_read8(cpu,0x7E,(unsigned short)(cpu->D + 0x0006)) == 0x05) {"
            " cpu_write8(cpu,0x7E,(unsigned short)(cpu->D + 0x0006), 0x04); } }"
        ),
    },
    {
        "marker": "/*WS-WING*/",
        # KoopaWingGfxRt only (see header comment). Anchor on the entry
        # of block $019E3C — after the vertical-offscreen early return,
        # before vanilla's x store and its 16-bit high-byte cull. At
        # block entry X is still the sprite slot and nothing has been
        # repurposed, so the wing's table index is reconstructed exactly
        # the way the block itself does ($157C,x << 1 + frame in DP $02)
        # and the OAM index comes from $15EA,x. The snippet only writes
        # the bytes vanilla skips in the margins (y/tile/attr + size with
        # x-high); registers, flags and stack are untouched, so vanilla's
        # in-window path and its own skip both proceed unchanged (no
        # double-write: the margin makes vanilla's Z test fail).
        # Unscoped: every emitted copy of the block is the wing body
        # (12 copies across the DrawWingTiles_* entry/variant functions).
        "func_match": "",
        "anchor": "cpu_trace_block(cpu, 0x019E3C)",
        "snippet": (
            " /*WS-WING*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14) {"
            " int _we = g_ws_extra > 95 ? 95 : g_ws_extra;"
            " static const signed char _wdx[4] = {-1, -9, 9, 9};"
            " static const unsigned char _wdy[4] = {0xFC, 0xF4, 0xFC, 0xF4};"
            " static const unsigned char _wtl[4] = {0x5D, 0xC6, 0x5D, 0xC6};"
            " static const unsigned char _wpr[4] = {0x46, 0x46, 0x06, 0x06};"
            " static const unsigned char _wsz[4] = {0x00, 0x02, 0x00, 0x02};"
            " unsigned int _wspr = cpu->X & 0xFFu;"
            " unsigned int _wi = (unsigned int)(((cpu_read8(cpu,0x7E,(unsigned short)(0x157C+_wspr)) << 1)"
            " + cpu_read8(cpu,0x7E,(unsigned short)(cpu->D + 0x0002))) & 3u);"
            " int _wwx = (cpu_read8(cpu,0x7E,(unsigned short)(0x00E4+_wspr))"
            " | (cpu_read8(cpu,0x7E,(unsigned short)(0x14E0+_wspr))<<8)) + _wdx[_wi];"
            " int _wsx = (int)(short)(unsigned short)((unsigned int)(_wwx"
            " - (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8))) & 0xFFFFu);"
            " if ((_wsx >= -_we && _wsx < 0) || (_wsx >= 256 && _wsx < 256 + _we)) {"
            " unsigned int _wo = cpu_read8(cpu,0x7E,(unsigned short)(0x15EA+_wspr));"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0301u+_wo), (unsigned char)("
            " cpu_read8(cpu,0x7E,(unsigned short)(0x00D8+_wspr))"
            " - cpu_read8(cpu,0x7E,0x001C) + _wdy[_wi]));"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0302u+_wo), _wtl[_wi]);"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0303u+_wo), (unsigned char)("
            " cpu_read8(cpu,0x7E,(unsigned short)(cpu->D + 0x0064)) | _wpr[_wi]));"
            " cpu_write8(cpu,0x7E,(unsigned short)(0x0460u+(_wo>>2)),"
            " (unsigned char)(_wsz[_wi] | 0x01)); } } }"
        ),
    },
    # WS-SPAWN (see header comment): bank-02 ParseLevelSpriteList only.
    {
        "marker": "/*WS-SPAWN*/",
        "func_match": "ParseLevelSpriteList",
        "anchor": "cpu_trace_block(cpu, 0x02A828)",
        "snippet": (
            " /*WS-SPAWN*/ { extern bool g_ws_active; extern int g_ws_extra;"
            " if (g_ws_active && cpu_read8(cpu,0x7E,0x0100) == 0x14"
            " && !(cpu_read8(cpu,0x7E,0x005B) & 1)) {"
            " unsigned int _wd = cpu_read8(cpu,0x7E,0x0055);"
            " if (_wd == 0 || _wd == 2) {"
            " unsigned int _wrun = (unsigned int)cpu_read8(cpu,0x7E,0x0014) >> 1;"
            " int _woff;"
            " if (_wrun & 1) { _woff = g_ws_extra; }"
            " else { int _wnc = g_ws_extra / 16 + 1;"
            " _woff = 16 * (int)((_wrun >> 1) % (unsigned int)_wnc); }"
            " int _wcol = (cpu_read8(cpu,0x7E,0x001A) | (cpu_read8(cpu,0x7E,0x001B)<<8))"
            " + ((_wd == 0) ? -(0x30 + _woff) : (0x120 + _woff));"
            " if (_wcol >= 0) {"
            " cpu_write8(cpu,0x7E,(unsigned short)(cpu->D + 0x0000),"
            " (unsigned char)(_wcol & 0xF0));"
            " cpu_write_a_m(cpu, (uint16)((_wcol >> 8) & 0xFF)); } } } }"
        ),
    },
]

# Every marker any injection mode can leave behind (prologues + block patches).
ALL_MARKERS = (MARKER, "/*WS-FLAG*/", "/*WS-DESPAWN*/", "/*WS-SPAWN*/",
               "/*WS-CHAIN*/", "/*WS-SLOT*/", "/*WS-RELOC*/", "/*WS-WING*/",
               "/*WS-COOP-TILE*/", "/*WS-COOP-ROW*/")


def strip_injections(text):
    """Remove every injected ` /*WS-...*/ { ... }` snippet from one file's
    text. Exact inverse of injection: each snippet is a single ` MARKER {`
    block appended to a generated line, with no string literals containing
    braces, so scanning from the marker's opening brace to balance removes
    precisely what was added (including the leading space the injector
    prepends). Returns (text, n_removed)."""
    n = 0
    for mk in ALL_MARKERS:
        while True:
            i = text.find(mk)
            if i < 0:
                break
            start = i - 1 if i > 0 and text[i - 1] == " " else i
            j = text.index("{", i + len(mk))
            depth = 0
            k = j
            while True:
                c = text[k]
                if c == "{":
                    depth += 1
                elif c == "}":
                    depth -= 1
                    if depth == 0:
                        break
                k += 1
            text = text[:start] + text[k + 1:]
            n += 1
    return text, n


# Recognize a generated function definition header to scope block patches.
_FUNC_HDR = re.compile(r"^RecompReturn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*CpuState")


def apply_block_patches(text):
    """Apply BLOCK_PATCHES to one file's text, function-scoped. Returns (text, n)."""
    n = 0
    for p in BLOCK_PATCHES:
        if p["anchor"] not in text:
            continue
        out = []
        cur_func = None
        for line in text.splitlines(keepends=True):
            mh = _FUNC_HDR.match(line)
            if mh:
                cur_func = mh.group(1)
            if (p["anchor"] in line and p["marker"] not in line
                    and cur_func and p["func_match"] in cur_func):
                line = line.rstrip("\n") + p["snippet"] + "\n"
                n += 1
            out.append(line)
        text = "".join(out)
    return text, n

# Matches a generated function DEFINITION (opening brace), not a forward
# declaration (which ends in ';'). Captures the base name and the _M?X? suffix.
#   RecompReturn  SomeName_M1X1 ( CpuState *cpu ) {
DEF_RE = re.compile(
    r"^RecompReturn\s+([A-Za-z_][A-Za-z0-9_]*?)(_M[01]X[01])\s*"
    r"\(\s*CpuState\s*\*\s*cpu\s*\)\s*\{",
    re.MULTILINE,
)


def parse_manifest(path):
    """Return list of (base_name, override_symbol, variant_or_None)."""
    rules = []
    if not os.path.isfile(path):
        return rules
    with open(path, "r", encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if "->" not in line:
                sys.exit(f"apply_overrides: malformed manifest line: {raw!r}")
            lhs, rhs = line.split("->", 1)
            base = lhs.strip()
            parts = rhs.split()
            override = parts[0].strip()
            variant = parts[1].strip() if len(parts) > 1 else None
            rules.append((base, override, variant))
    return rules


def prologue(override_symbol):
    return (
        f" {MARKER} {{ extern bool g_ws_active;"
        f" extern RecompReturn {override_symbol}(CpuState *cpu);"
        f" if (g_ws_active) return {override_symbol}(cpu); }}"
    )


def apply_to_text(text, rules):
    """Return (new_text, n_injected). Idempotent."""
    by_base = {}
    for base, override, variant in rules:
        by_base.setdefault(base, []).append((override, variant))

    injected = 0

    def repl(m):
        nonlocal injected
        whole = m.group(0)
        base, suffix = m.group(1), m.group(2)
        cands = by_base.get(base)
        if not cands:
            return whole
        # Pick a rule whose variant matches this definition (or is unscoped).
        chosen = None
        for override, variant in cands:
            if variant is None or variant == suffix[1:]:  # suffix like '_M1X1'
                chosen = override
                break
        if chosen is None:
            return whole
        # The prologue is appended immediately after the matched opening brace,
        # so it is not part of `whole`. Check the original text at the match
        # boundary; testing `MARKER in whole` made repeated CMake invocations
        # stack duplicate fireball prologues despite this tool's idempotence
        # contract.
        after = text[m.end():m.end() + len(MARKER) + 2]
        if after.lstrip().startswith(MARKER):
            return whole
        return whole + prologue(chosen)

    new_text = DEF_RE.sub(repl, text)
    # Count injections by counting freshly added markers vs pre-existing.
    return new_text, new_text.count(MARKER) - text.count(MARKER)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen-dir", default="src/gen")
    ap.add_argument(
        "--manifest", default="overrides/widescreen/overrides.manifest"
    )
    ap.add_argument(
        "--check",
        action="store_true",
        help="verify every manifest base matched at least one definition",
    )
    ap.add_argument(
        "--restore",
        action="store_true",
        help="remove every injected snippet, restoring pristine gen output",
    )
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    if args.restore:
        if not os.path.isdir(args.gen_dir):
            sys.exit(f"apply_overrides: gen dir not found: {args.gen_dir}")
        total = 0
        for name in sorted(os.listdir(args.gen_dir)):
            if not name.endswith(".c"):
                continue
            path = os.path.join(args.gen_dir, name)
            with open(path, "r", encoding="utf-8", newline="") as f:
                text = f.read()
            new_text, n = strip_injections(text)
            if n:
                with open(path, "w", encoding="utf-8", newline="") as f:
                    f.write(new_text)
                total += n
                if args.verbose:
                    print(f"apply_overrides: {name}: removed {n} injection(s)")
        print(f"apply_overrides: restored pristine gen ({total} injection(s) removed)")
        return 0

    rules = parse_manifest(args.manifest)
    if not rules and not BLOCK_PATCHES:
        if args.verbose:
            print("apply_overrides: no active rules — authentic build, no-op")
        return 0

    if not os.path.isdir(args.gen_dir):
        sys.exit(f"apply_overrides: gen dir not found: {args.gen_dir}")

    matched_bases = set()
    total = 0
    for name in sorted(os.listdir(args.gen_dir)):
        if not name.endswith(".c"):
            continue
        path = os.path.join(args.gen_dir, name)
        # newline="" both ways: never translate the generator's LF line
        # endings (text-mode writes used to silently CRLF-convert every
        # patched bank, breaking byte-exact --restore round-trips).
        with open(path, "r", encoding="utf-8", newline="") as f:
            text = f.read()
        # Track which bases exist in this file before substitution.
        for m in DEF_RE.finditer(text):
            if m.group(1) in {b for b, _, _ in rules}:
                matched_bases.add(m.group(1))
        new_text, n = apply_to_text(text, rules)
        new_text, nb = apply_block_patches(new_text)
        if n or nb:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new_text)
            total += n + nb
            if args.verbose:
                print(f"apply_overrides: {name}: injected {n} prologue(s), {nb} block patch(es)")

    if args.check:
        missing = {b for b, _, _ in rules} - matched_bases
        if missing:
            sys.exit(
                "apply_overrides: manifest bases never matched a definition: "
                + ", ".join(sorted(missing))
            )

    print(f"apply_overrides: injected {total} dispatch prologue(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
