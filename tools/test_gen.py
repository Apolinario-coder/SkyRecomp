"""Function-boundary regressions; no game files needed: python tools/test_gen.py."""
import unittest
from collections import Counter

from gen import Module, SKY_TIMING_STORES
from lifter import Lifter
from patches import (BEETLE_TIMER_SITE, DAMAGE_SHIFT_SITE, DASH_CAP_SITE, DASH_START_SITE, DASH_USE_SITE,
                     DASH_GAUGE_SITE, DROP_END_SITE, DROP_KIND_SITES, DROP_THIN_SITE, FLIGHT_SPEED_SITE,
                     GAUGE_PICTURE_SITES, HERO_UNLOCK_SITE, WALK_SPEED_SITE,
                     SKY_PATCHES, STAMINA_STORE_SITE, TREASURE_ROLL_SITES)


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
                        self.assertIn('*(f32a *)&c->v[0] = rt_sky_frame_step(*(f32a *)&c->v[0])', code)
                        self.assertIn('*(f32a *)&c->v[1] = rt_sky_frame_ms(*(f32a *)&c->v[1])', code)
                        self.assertNotIn('VSF32(', code)  # (simd.h isn't included with --simd asm)


    def test_sky_patches_only_for_sky(self):
        words = {STAMINA_STORE_SITE: 0xb90002e8, BEETLE_TIMER_SITE: 0x4b090108, HERO_UNLOCK_SITE: 0x38696908,
                 DAMAGE_SHIFT_SITE: 0x1ac822b5, DASH_USE_SITE: 0x51000529, DASH_START_SITE: 0x52800069,
                 DASH_CAP_SITE: 0x7100091f, FLIGHT_SPEED_SITE: 0x1e272863, DROP_THIN_SITE: 0x14000028,
                 DROP_END_SITE: 0x54000822, DASH_GAUGE_SITE: 0x79e99529,
                 WALK_SPEED_SITE: 0xbd04bac8}
        words.update({a: 0x39400296 for a in DROP_KIND_SITES})
        words.update({a: w for a, (w, _, _) in TREASURE_ROLL_SITES.items()})
        words.update({a: 0x910072a1 for a in GAUGE_PICTURE_SITES})
        self.assertEqual(set(words), set(SKY_PATCHES))
        for pc, word in words.items():
            for sky in (False, True):
                with self.subTest(pc=hex(pc), sky=sky):
                    m, _ = self.module(9)
                    m.lo = m.base = pc
                    m.words = [word, words.get(pc + 4, 0xD503201F), 0xD65F03C0]  # (the next one may be a site too)
                    m.hi = m.lo + 12
                    m.is_las, m.is_skyward = False, sky
                    m.lifter = Lifter(m.base, 'c')
                    code = '\n'.join(m.emit_function(m.lo, m.hi, set(), {}, {}))
                    self.assertEqual(f'/* {pc:#x}: ' in code, sky)


if __name__ == '__main__':
    unittest.main()
