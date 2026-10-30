"""Function-boundary regressions; no game files needed: python tools/test_gen.py."""
import unittest
from collections import Counter

from gen import Module, SKY_BUILDS, sky_stores_for
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

    def test_sky_timing_hooks_follow_the_build(self):
        base = 0x7100004000  # the main module's base
        builds = {'1.0.0': {base + o: v for o, v in SKY_BUILDS['d824d4b1bbd45d03668380d6d041ba4ca00a64ff'].items()},
                  '1.0.1': {base + o: v for o, v in SKY_BUILDS['5441af3c89f97e1d'].items()}}
        for name, stores in builds.items():
            for simd in ('c', 'asm'):
                for sky in (False, True):
                    with self.subTest(build=name, simd=simd, sky=sky):
                        m, _ = self.module(9)
                        m.lo = m.base = min(stores)
                        m.words = [0xD503201F] * ((max(stores) - m.lo) // 4 + 2)
                        for pc, (_, _, word) in stores.items():
                            m.words[(pc - m.lo) // 4] = word
                        m.words[-1] = 0xD65F03C0
                        m.hi = m.lo + len(m.words) * 4
                        m.is_las = False
                        m.sky_stores = stores if sky else None
                        m.lifter = Lifter(m.base, simd)
                        code = '\n'.join(m.emit_function(m.lo, m.hi, set(), {}, {}))
                        self.assertEqual(code.count('extern float rt_sky_frame_'), 2 if sky else 0)
                        if sky:
                            self.assertIn('*(f32a *)&c->v[0] = rt_sky_frame_step(*(f32a *)&c->v[0])', code)
                            self.assertIn('*(f32a *)&c->v[1] = rt_sky_frame_ms(*(f32a *)&c->v[1])', code)
                            self.assertIn('ST32(c->x[8] + 0xd38ull', code)
                            self.assertIn('ST32(c->x[8] + 0xd3cull', code)
                            self.assertNotIn('VSF32(', code)  # (simd.h isn't included with --simd asm)

    def test_sky_timing_refuses_a_binary_with_other_instructions(self):
        stores = {0x7100004000 + o: v for o, v in SKY_BUILDS['5441af3c89f97e1d'].items()}
        m, _ = self.module(9)
        m.lo = m.base = min(stores)
        m.words = [0xD503201F] * ((max(stores) - m.lo) // 4 + 2)  # no store at the timing addresses
        m.words[-1] = 0xD65F03C0
        m.hi = m.lo + len(m.words) * 4
        m.is_las = False
        m.sky_stores = stores
        m.lifter = Lifter(m.base, 'c')
        with self.assertRaises(AssertionError):
            m.emit_function(m.lo, m.hi, set(), {}, {})

    def test_known_builds_by_build_id_prefix(self):
        self.assertEqual(sky_stores_for('D824D4B1BBD45D03668380D6D041BA4CA00A64FF'),
                         SKY_BUILDS['d824d4b1bbd45d03668380d6d041ba4ca00a64ff'])
        self.assertEqual(sky_stores_for('5441af3c89f97e1d' + '00' * 12), SKY_BUILDS['5441af3c89f97e1d'])
        self.assertIsNone(sky_stores_for('00' * 32))
        self.assertIsNone(sky_stores_for('5441af3c89f97e1e'))  # one byte off: not 1.0.1
        self.assertIsNone(sky_stores_for(''))
        self.assertIsNone(sky_stores_for(None))

    def test_1_0_1_timing_stores_are_the_same_instructions(self):
        # Read from the 1.0.1 main module (decoded text, offsets 0x104a4fc and 0x104a520): str s0,[x8,#0xd38] and
        # str s1,[x8,#0xd3c]. The 1.0.0 offsets hold other code in 1.0.1, so the hooks moved with the build.
        s = SKY_BUILDS['5441af3c89f97e1d']  # (offsets from the main module's base)
        self.assertEqual(s[0x104a4fc], (0, 0xd38, 0xbd0d3900))
        self.assertEqual(s[0x104a520], (1, 0xd3c, 0xbd0d3d01))
        self.assertNotIn(0x104a1ac, s)


if __name__ == '__main__':
    unittest.main()
