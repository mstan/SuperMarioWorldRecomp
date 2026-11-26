/* Reuse the established adapter fixture without running its broader campaign. */
#define main falcon_step_guard_legacy_main
#include "falcon_step_guard_test.c"
#undef main

int main(void)
{
    CpuState cpu, before;
    static uint8 before_ram[sizeof(g_ram)];
    memset(g_ram, 0, sizeof(g_ram));
    if (!smw_captain_falcon_register() ||
        !snes_foreign_select(SMW_CAPTAIN_FALCON_ID)) return fail("select Falcon");
    misc_game_mode = 0x14;
    player_xpos = 0x070F;
    player_ypos = 0x0160;
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_FOREIGN);
    SmwFalconBeforePhysics(NULL); /* leave a real adapter frame pending */
    snes_foreign_select(NULL);
    snes_foreign_set_ownership(FOREIGN_OWNERSHIP_NATIVE);
    for (unsigned m = 0; m != 2; ++m) {
        memset(&cpu, 0xA5, sizeof(cpu));
        cpu.ram = g_ram;
        cpu.m_flag = (uint8)m;
        cpu.x_flag = 1;
        cpu.P = (uint8)(0x95 | (m << 5));
        memcpy(&before, &cpu, sizeof(cpu));
        memcpy(before_ram, g_ram, sizeof(g_ram));
        for (unsigned owner = 0; owner != 4; ++owner)
            SmwFalconBeforeCrushCheck(&cpu);
        if (memcmp(&before, &cpu, sizeof(cpu)) ||
            memcmp(before_ram, g_ram, sizeof(g_ram)))
            return fail("disabled E9FB observer preserves CPU/flags and physics RAM");
    }
    puts("falcon_disabled_observer_test: PASS (both widths, four owner calls)");
    return 0;
}
