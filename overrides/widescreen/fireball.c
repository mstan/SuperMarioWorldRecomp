// Widescreen override: Mario's fireball (and the shared extended-sprite
// fireball graphics routine) no longer culls/deletes at the 4:3 edges.
//
// Vanilla CODE_02A1A7 (GenericExtendedSpriteGFXRt_FireballEntry, $02A1A7) draws
// one OAM tile for the fireball and culls it with a *private* window — it does
// NOT go through GetDrawInfo (which WS-FLAG widens for normal sprites). The
// horizontal cull deletes the extended sprite (CODE_02A211 clears
// ExtSpriteNumber) the moment screen-x leaves [0,256), and it writes the OAM
// size byte as 0 (x-high / 9th x bit clear). In widescreen that makes a thrown
// fireball vanish at the visible 4:3 edge.
//
// This is the same defect class as the para-koopa wing (WS-WING): a sprite that
// writes its own OAM with an embedded 4:3 cull. The fix mirrors WS-WING/WS-FLAG:
//   - widen the horizontal keep window to [-extra, 256+extra) so the fireball
//     survives + draws across the full widescreen view, and
//   - set the OAM x-high bit when the tile sits in either margin, so the PPU's
//     widened sprite-x wrap threshold (256+extra) places it on the correct side.
// The vertical cull is left exactly as vanilla (widescreen is horizontal only).
//
// Gated on g_ws_active + game mode $0100 == 0x14 (player-controlled level main),
// like every WS patch; with widescreen off the original generated routine runs
// and the build is byte-identical. The alternate "+$100 OAM region" draw path in
// MarioFireball is dead code (it needs IRQNMICommand bit6, which no valid value
// of $0D9B sets), so this single routine covers all live fireball drawing.
//
// This file is the override body referenced by overrides/widescreen/
// overrides.manifest (rule: GenericExtendedSpriteGFXRt_FireballEntry ->
// Override_FireballGfx). See overrides/README.md for the override contract.

#include "cpu_state.h"
#include "cpu_trace.h"
#include "common_cpu_infra.h"

#include "widescreen.h"  // g_ws_active, g_ws_extra

// Shared RTS epilogue, replicated verbatim from the generated leaf body so the
// override returns identically to vanilla in every (balanced / trampoline /
// ancestor) case. `ret_pc` is the post-RTS sentinel the generated code used
// ($02A210 for the draw exit, $02A216 for the delete exit).
static RecompReturn fireball_gfx_rts(CpuState *cpu, uint16 entry_s, uint8 hrv,
                                     uint32 ret_pc) {
    uint16 _ret_s = cpu->S;  /* RTS pop hardware return frame */
    cpu->S = (uint16)(cpu->S + 1);
    uint16 _rpcl = (uint16)cpu_read8(cpu, 0x00, cpu->S);
    cpu->S = (uint16)(cpu->S + 1);
    uint16 _rpch = (uint16)cpu_read8(cpu, 0x00, cpu->S);
    uint8 _rpb = cpu->PB;
    uint32 _rpc = (uint32)((((_rpch << 8) | _rpcl) + 1) & 0xFFFFu);
    uint32 _rpc24 = ((uint32)_rpb << 16) | _rpc;
    if (hrv && _ret_s == entry_s) {
        RecompStackPop();
        return RECOMP_RETURN_NORMAL;  /* RTS host return */
    }
    if (_ret_s != entry_s && !cpu_dispatch_has_entry(cpu, _rpc24)) {
        int _anc_skip = cpu_resolve_ancestor_skip(_ret_s);
        if (_anc_skip >= 0) {
            cpu_trace_mark_nlr_exit(BD_EXIT_KIND_TRAMPOLINE);
            RecompStackPop();
            return (RecompReturn)_anc_skip;  /* RTS return-to-ancestor */
        }
    }
    cpu_trace_mark_nlr_exit(BD_EXIT_KIND_TRAMPOLINE);
    RecompStackPop();
    return cpu_dispatch_pc_from(cpu, _rpc24, (uint16)(entry_s + 2u), ret_pc);
}

RecompReturn Override_FireballGfx(CpuState *cpu) {
    uint16 entry_s = cpu->S;
    uint8  hrv     = cpu->host_return_valid;

    unsigned int slot = (unsigned int)(cpu->X & 0xFFFFu);  // extended-sprite slot
    unsigned int oi   = (unsigned int)(cpu->Y & 0xFFFFu);  // OAM byte index (caller-set)

    // The override fully replaces the generated routine while g_ws_active is true
    // (it must NOT call back into it — the prologue would re-enter and recurse),
    // so it reproduces vanilla too. The widescreen widening only applies in
    // player-controlled level main ($0100 == 0x14), mirroring every other WS
    // patch; elsewhere extra = 0 collapses the keep window to the authentic
    // [0,256) and leaves x-high clear, i.e. byte-identical to vanilla.
    int extra = 0;
    if (cpu_read8(cpu, 0x7E, 0x0100) == 0x14) {
        extra = g_ws_extra; if (extra > 95) extra = 95;
    }

    // 16-bit camera-relative screen position (mirrors the vanilla SBCs).
    // Direct-page reads use bank $7E + cpu->D; the extended-sprite tables and OAM
    // are DB-banked absolute (DB = $02 here, mirroring WRAM) — matched exactly.
    int camx = cpu_read8(cpu, 0x7E, (uint16)(cpu->D + 0x001A))
             | (cpu_read8(cpu, 0x7E, (uint16)(cpu->D + 0x001B)) << 8);
    int camy = cpu_read8(cpu, 0x7E, (uint16)(cpu->D + 0x001C))
             | (cpu_read8(cpu, 0x7E, (uint16)(cpu->D + 0x001D)) << 8);
    int fbx  = cpu_read8(cpu, cpu->DB, (uint16)(0x171F + slot))
             | (cpu_read8(cpu, cpu->DB, (uint16)(0x1733 + slot)) << 8);
    int fby  = cpu_read8(cpu, cpu->DB, (uint16)(0x1715 + slot))
             | (cpu_read8(cpu, cpu->DB, (uint16)(0x1729 + slot)) << 8);
    int sx = (int)(short)(unsigned short)((unsigned int)(fbx - camx) & 0xFFFFu);
    int sy = (int)(short)(unsigned short)((unsigned int)(fby - camy) & 0xFFFFu);
    int sy_low = sy & 0xFF;

    // Vertical keep is unchanged (vanilla: screen-y in [0,0xF0)); horizontal keep
    // is widened from [0,256) to [-extra, 256+extra).
    int vert_ok  = (sy >= 0 && sy < 256) && (sy_low < 0xF0);
    int horiz_ok = (sx >= -extra) && (sx < 256 + extra);

    if (!vert_ok || !horiz_ok) {
        // CODE_02A211: clear the extended sprite (off-screen beyond the view).
        cpu_write8(cpu, cpu->DB, (uint16)(0x170B + slot), 0x00);
        cpu->_flag_Z = 1; cpu->_flag_N = 0;
        cpu->P = (uint8)((cpu->P & ~0x82) | 0x02);
        return fireball_gfx_rts(cpu, entry_s, hrv, 0x02A216u);
    }

    // CODE_02A1D5 .. CODE_02A204: draw one OAM tile.
    // _0 = X-flip bit from travel direction (AND #$80 / EOR #$80 / LSR).
    unsigned char xspeed = cpu_read8(cpu, cpu->DB, (uint16)(0x1747 + slot));
    unsigned char flip   = (unsigned char)((((xspeed & 0x80) ^ 0x80) >> 1)); // 0x40 / 0x00

    cpu_write8(cpu, cpu->DB, (uint16)(0x0201 + oi), (unsigned char)sy_low);       // OAMTileYPos
    cpu_write8(cpu, cpu->DB, (uint16)(0x0200 + oi), (unsigned char)(sx & 0xFF));  // OAMTileXPos

    unsigned int prio = (unsigned int)cpu_read8(cpu, cpu->DB, (uint16)(0x1779 + slot)); // ExtSpritePriority
    unsigned int ti   = ((unsigned int)cpu_read8(cpu, 0x7E, (uint16)(cpu->D + 0x0014)) >> 2) & 3u; // EffFrame>>2

    unsigned char tile = cpu_read8(cpu, cpu->DB, (uint16)(0xA15B + ti));  // FireballTiles
    cpu_write8(cpu, cpu->DB, (uint16)(0x0202 + oi), tile);                // OAMTileNo

    unsigned char a15f  = cpu_read8(cpu, cpu->DB, (uint16)(0xA15F + ti)); // DATA_02A15F
    unsigned char props = cpu_read8(cpu, 0x7E, (uint16)(cpu->D + 0x0064)); // SpriteProperties
    unsigned char attr  = (unsigned char)((a15f ^ flip) | props);
    if (prio != 0) attr = (unsigned char)((attr & 0xCF) | 0x10);
    cpu_write8(cpu, cpu->DB, (uint16)(0x0203 + oi), attr);                // OAMTileAttr

    // OAM size/x-high byte ($0420 + tile>>2). Vanilla writes 0 (8x8, x-high 0).
    // Set the x-high bit only when the tile sits in a margin so the PPU's widened
    // wrap threshold renders it on the correct side; on-screen stays 0 (vanilla).
    unsigned char xhigh = (sx < 0 || sx >= 256) ? 0x01 : 0x00;
    cpu_write8(cpu, cpu->DB, (uint16)(0x0420 + (oi >> 2)), xhigh);        // OAMTileSize

    // vanilla tail: LDX CurSpriteProcess.
    unsigned int csp = (unsigned int)cpu_read8(cpu, cpu->DB, (uint16)(0x15E9));
    cpu->X = (uint16)(cpu->x_flag ? (csp & 0xFFu) : csp);
    cpu->_flag_Z = (csp == 0) ? 1 : 0;
    cpu->_flag_N = ((csp & 0x80) != 0) ? 1 : 0;
    cpu->P = (uint8)((cpu->P & ~0x82) | (cpu->_flag_Z ? 0x02 : 0) | (cpu->_flag_N ? 0x80 : 0));

    return fireball_gfx_rts(cpu, entry_s, hrv, 0x02A210u);
}
