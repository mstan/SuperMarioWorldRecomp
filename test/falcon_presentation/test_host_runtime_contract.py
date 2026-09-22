"""Regression checks for the owner-cache helper process boundary."""
from pathlib import Path
import unittest


SOURCE = (Path(__file__).resolve().parents[2] / "src/mods/falcon/"
          "smw_falcon_presentation_runtime.c")


class FalconHostRuntimeContractTests(unittest.TestCase):
    def test_hostile_paths_are_never_formatted_into_a_shell_command(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertNotIn("system(", source)
        self.assertNotIn("quote_safe", source)
        self.assertIn('"--rom", (char *)owner_rom_path', source)
        self.assertIn('"--cache-root", (char *)root', source)
        self.assertIn('"--result-file",', source)
        self.assertIn('(char *)result, NULL', source)
        self.assertIn("CreateProcessW(wide_helper, wide_command", source)
        self.assertIn("CREATE_NO_WINDOW", source)
        self.assertIn("execv(helper, argv)", source)
        self.assertIn("k_audio_sha256[11][32]", source)
        self.assertIn("join_path(audio_dir, audio_dir_size, cache, \"audio\")", source)
        self.assertIn("smw_falcon_audio_activate(s_validated_audio_dir)", source)
        self.assertIn("approved owner cache audio disabled", source)
        self.assertIn("smw_falcon_audio_reset();", source)
        self.assertIn("smw_falcon_presentation_foot_anchor_y", source)
        self.assertIn("player_screen_y + 32", source)
        # Every shell metacharacter is valid inside a path argument because the
        # argv launcher never parses it as shell source.
        hostile = r"C:\owner&cache|<bad>^%!$`;space"
        self.assertTrue(hostile.startswith("C:\\"))

    def test_course_clear_is_presentation_only_and_keeps_keyhole_falcon(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("static int falcon_controller_selected(void)", source)
        self.assertIn("static int course_clear_active(void)", source)
        self.assertIn("!falcon_controller_selected()", source)
        self.assertIn("misc_game_mode == 0x14 && player_current_state == 0", source)
        self.assertIn("(timer_end_level != 0 || timer_end_level_via_keyhole != 0)", source)
        self.assertIn("return misc_game_mode == 0x0b;", source)
        self.assertNotIn("flag_show_victory_pose_during_level_end != 0", source)
        self.assertIn("return controllable() || death_active() || course_clear_active() ||", source)
        self.assertIn("if (!presentation_active()) { s_suppression_active = 0; return; }", source)
        self.assertIn("if (!presentation_active() || !pixels", source)
        # Carry OAM mutation remains live-control-only; Course Clear merely
        # hides PlayerGFXRt and draws Falcon over the native score script.
        self.assertIn("if (!ppu || !controllable()) return;", source)
        self.assertIn("pose.state = FALCON_PRESENT_WALK;", source)
        self.assertIn("pose.frame = (float)(counter_global_frames >> 1);", source)
        self.assertIn("no Captain appeal/taunt", source)

    def test_scripted_water_transition_keeps_falcon_presentation(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("static int scripted_water_active(void)", source)
        self.assertIn("static int scripted_pipe_active(void)", source)
        self.assertIn("active_pipe_handoff()", source)
        self.assertIn("flag_underwater_level != 0", source)
        self.assertIn("player_timer_pipe_warping != 0", source)
        self.assertIn("(player_pipe_action != 0 && player_pipe_action < 4)", source)
        self.assertIn("scripted_pipe_active();", source)
        self.assertIn("scripted_pipe_active()) &&", source)

    def test_powerup_and_damage_animation_keep_full_size_falcon(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("static int powerup_animation_active(void)", source)
        self.assertIn("Native GameMode14 player-state table:", source)
        self.assertIn("1 PowerDown, 2 Grow, 3 GotCape, 4 GotFlower", source)
        self.assertIn("return player_current_state >= 1 && player_current_state <= 4;", source)
        self.assertIn("else if (powerup_animation_active())", source)
        self.assertIn("pose.state = FALCON_PRESENT_IDLE;", source)
        self.assertIn("pose.frame = 0.0f;", source)


if __name__ == "__main__":
    unittest.main()
