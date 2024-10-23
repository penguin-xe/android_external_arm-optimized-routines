/*
 * Single-precision vector log function.
 *
 * Copyright (c) 2019-2024, Arm Limited.
 * SPDX-License-Identifier: MIT OR Apache-2.0 WITH LLVM-exception
 */

#include "mathlib.h"
#include "v_math.h"
#include "test_defs.h"

static const struct data
{
  uint32x4_t off, offset_lower_bound;
  uint16x8_t special_bound;
  uint32x4_t mantissa_mask;
  float32x4_t poly[7];
  float32x4_t ln2;
} data = {
  /* 3.34 ulp error.  */
  .poly = { V4 (-0x1.3e737cp-3f), V4 (0x1.5a9aa2p-3f), V4 (-0x1.4f9934p-3f),
	    V4 (0x1.961348p-3f), V4 (-0x1.00187cp-2f), V4 (0x1.555d7cp-2f),
	    V4 (-0x1.ffffc8p-2f) },
  .ln2 = V4 (0x1.62e43p-1f),
  /* Lower bound is the smallest positive normal float 0x00800000. For
     optimised register use subnormals are detected after offset has been
     subtracted, so lower bound is 0x0080000 - offset (which wraps around).  */
  .offset_lower_bound = V4 (0x00800000 - 0x3f2aaaab),
  .special_bound = V8 (0x7f00), /* top16(asuint32(inf) - 0x00800000).  */
  .off = V4 (0x3f2aaaab),	/* 0.666667.  */
  .mantissa_mask = V4 (0x007fffff)
};

#define P(i) d->poly[7 - i]

static float32x4_t VPCS_ATTR NOINLINE
special_case (float32x4_t p, uint32x4_t u_off, float32x4_t y, float32x4_t r2,
	      uint16x4_t cmp, const struct data *d)
{
  /* Fall back to scalar code.  */
  return v_call_f32 (logf, vreinterpretq_f32_u32 (vaddq_u32 (u_off, d->off)),
		     vfmaq_f32 (p, y, r2), vmovl_u16 (cmp));
}

float32x4_t VPCS_ATTR V_NAME_F1 (log) (float32x4_t x)
{
  const struct data *d = ptr_barrier (&data);
  float32x4_t n, p, q, r, r2, y;
  uint32x4_t u, u_off;
  uint16x4_t cmp;

  /* To avoid having to mov x out of the way, keep u after offset has been
     applied, and recover x by adding the offset back in the special-case
     handler.  */
  u_off = vreinterpretq_u32_f32 (x);

  /* x = 2^n * (1+r), where 2/3 < 1+r < 4/3.  */
  u_off = vsubq_u32 (u_off, d->off);
  n = vcvtq_f32_s32 (
      vshrq_n_s32 (vreinterpretq_s32_u32 (u_off), 23)); /* signextend.  */
  u = vandq_u32 (u_off, d->mantissa_mask);
  u = vaddq_u32 (u, d->off);
  r = vsubq_f32 (vreinterpretq_f32_u32 (u), v_f32 (1.0f));

  cmp = vcge_u16 (vsubhn_u32 (u_off, d->offset_lower_bound),
		  vget_low_u16 (d->special_bound));

  /* y = log(1+r) + n*ln2.  */
  r2 = vmulq_f32 (r, r);
  /* n*ln2 + r + r2*(P1 + r*P2 + r2*(P3 + r*P4 + r2*(P5 + r*P6 + r2*P7))).  */
  p = vfmaq_f32 (P (5), P (6), r);
  q = vfmaq_f32 (P (3), P (4), r);
  y = vfmaq_f32 (P (1), P (2), r);
  p = vfmaq_f32 (p, P (7), r2);
  q = vfmaq_f32 (q, p, r2);
  y = vfmaq_f32 (y, q, r2);
  p = vfmaq_f32 (r, d->ln2, n);

  if (unlikely (v_any_u16h (cmp)))
    return special_case (p, u_off, y, r2, cmp, d);
  return vfmaq_f32 (p, y, r2);
}

TEST_ULP (V_NAME_F1 (log), 2.9)
TEST_DISABLE_FENV_IF_NOT (V_NAME_F1 (log), WANT_SIMD_EXCEPT)
TEST_INTERVAL (V_NAME_F1 (log), 0, 0xffff0000, 10000)
TEST_INTERVAL (V_NAME_F1 (log), 0x1p-4, 0x1p4, 500000)
TEST_INTERVAL (V_NAME_F1 (log), 0, inf, 50000)
