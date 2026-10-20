"""Function-boundary regressions; no game files needed: python tools/test_gen.py."""
import unittest
from collections import Counter

from gen import Module, SKY_TIMING_STORES
from lifter import Lifter


class JumpTableBoundaryTest(unittest.TestCase):
    def module(self, gap, loaded=False):
        m = Module.__new__(Module)
        m.base = m.lo = 0x1000
        # ADR x11, base; ADD x11, x11, x12, LSL #2; ...; BR x11; base:
        target = m.lo + (gap + 3) * 4
        imm = target - m.lo
        adr = 0x1000000B | ((imm & 3) << 29) | ((imm >> 2) << 5)
        m.words = [adr, 0xF940010B if loaded else 0x8B0C096B] + [0xD503201F] * gap + [0xD61F0160, 0xD65F03C0]
        m.hi = m.lo + len(m.words) * 4
        m.lifter = Lifter(m.base, 'c')
        m.cache, m.stats = {}, Counter()
        return m, target

    def test_scheduled_tables_stay_local(self):
        for gap in (8, 9, 32):  # NPC/cave schedules exceed the old seven-instruction lookahead.
            with self.subTest(gap=gap):
                m, target = self.module(gap)
                self.assertTrue(m.jump_table_base(m.lo, target))
                self.assertTrue(m.computed_target(target - 4, m.lo))

    def test_loaded_callback_is_not_a_table(self):
        m, target = self.module(9, loaded=True)
        self.assertFalse(m.jump_table_base(m.lo, target))

    def test_outside_text_is_not_a_table(self):
        m, target = self.module(9)
        self.assertFalse(m.jump_table_base(m.lo, m.hi + 4))

    def test_vector_destination_does_not_overwrite_branch_register(self):
        for simd in ('c', 'asm'):
            with self.subTest(simd=simd):
                m, target = self.module(9)
                m.words[0] = (m.words[0] & ~31) | 9  # ADR x9, base
                m.words[-2] = 0xD61F0120  # BR x9
                m.words[1] = 0x8B0A0929   # ADD x9, x9, x10, LSL #2
                m.words[-3] = 0x4EB01E09  # MOV v9.16b, v16.16b
                m.lifter = Lifter(m.base, simd)
                self.assertTrue(m.computed_target(target - 4, m.lo))
                self.assertTrue(m.jump_table_base(m.lo, target))

    def test_fp_result_in_gpr_does_overwrite_branch_register(self):
        for simd in ('c', 'asm'):
            with self.subTest(simd=simd):
                m, target = self.module(9)
                m.words[-3] = 0x9E66000B  # FMOV x11, d0
                m.lifter = Lifter(m.base, simd)
                self.assertFalse(m.computed_target(target - 4, m.lo))

    def test_sky_timing_scales_both_registers_and_stores_only_for_sky(self):
        for simd in ('c', 'asm'):
            for sky in (False, True):
                with self.subTest(simd=simd, sky=sky):
                    m, _ = self.module(9)
                    m.lo = m.base = min(SKY_TIMING_STORES)
                    m.words = [0xD503201F] * ((max(SKY_TIMING_STORES) - m.lo) // 4 + 2)
                    for pc, (_, _, word) in SKY_TIMING_STORES.items():
                        m.words[(pc - m.lo) // 4] = word
                    m.words[-1] = 0xD65F03C0
                    m.hi = m.lo + len(m.words) * 4
                    m.is_las = False
                    m.is_skyward = sky
                    m.lifter = Lifter(m.base, simd)
                    code = '\n'.join(m.emit_function(m.lo, m.hi, set(), {}, {}))
                    self.assertEqual(code.count('extern float rt_sky_frame_'), 2 if sky else 0)
                    if sky:
                        self.assertIn('VSF32(0) = rt_sky_frame_step(VSF32(0))', code)
                        self.assertIn('VSF32(1) = rt_sky_frame_ms(VSF32(1))', code)


if __name__ == '__main__':
    unittest.main()
