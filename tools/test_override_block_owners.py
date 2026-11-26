import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('apply_overrides',
    Path(__file__).with_name('apply_overrides.py'))
hooks = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hooks)


class CrushOwners(unittest.TestCase):
    def test_all_current_physics_paths_and_widths(self):
        owners = ['CoopNative_00E9A1_M1X1',
                  'HandlePlayerLevelColl_00E9C8_M1X1',
                  'HandlePlayerLevelColl_00E9C8_M0X1',
                  'HandlePlayerLevelColl_00E9FB_M1X1']
        source = ''.join(f'RecompReturn {n}(CpuState *cpu) {{\n'
                        '  cpu_trace_block(cpu, 0x00E9FB);\n}\n'
                        for n in owners)
        patched, count = hooks.apply_block_patches(source)
        self.assertEqual(count, 4)
        self.assertEqual(patched.count('SmwFalconBeforeCrushCheck(cpu);'), 4)
        self.assertEqual(hooks.apply_block_patches(patched), (patched, 0))

    def test_unknown_owner_and_nearby_pc_are_not_patched(self):
        source = ('RecompReturn Unrelated_M1X1(CpuState *cpu) {\n'
                  '  cpu_trace_block(cpu, 0x00E9FB);\n}\n'
                  'RecompReturn CoopNative_00E9A1_M1X1(CpuState *cpu) {\n'
                  '  cpu_trace_block(cpu, 0x00E9FC);\n}\n')
        self.assertEqual(hooks.apply_block_patches(source), (source, 0))


if __name__ == '__main__':
    unittest.main()
