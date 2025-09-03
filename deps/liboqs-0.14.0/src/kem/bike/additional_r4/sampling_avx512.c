/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0"
 *
 * Written by Nir Drucker, Shay Gueron and Dusan Kostic,
 * AWS Cryptographic Algorithms Group.
 */

#include <assert.h>

#include "sampling_internal.h"

#define AVX512_INTERNAL
#include "x86_64_intrinsic.h"

// For improved performance, we process NUM_ZMMS amount of data in parallel.
#define NUM_ZMMS    (8)
#define ZMMS_QWORDS (QWORDS_IN_ZMM * NUM_ZMMS)

void secure_set_bits_avx512(OUT pad_r_t *   r,
                            IN const size_t first_pos,
                            IN const idx_t *wlist,
                            IN const size_t w_size)
{
  // The function assumes that the size of r is a multiple
  // of the cumulative size of used ZMM registers.
  assert((sizeof(*r) / sizeof(uint64_t)) % ZMMS_QWORDS == 0);

  // va vectors hold the bits of the output array "r"
  // va_pos_qw vectors hold the qw position indices of "r"
  // The algorithm works as follows:
  //   1. Initialize va_pos_qw with starting positions of qw's of "r"
  //      va_pos_qw = (7, 6, 5, 4, 3, 2, 1, 0);
  //   2. While the size of "r" is not exceeded:
  //   3.   For each w in wlist:
  //   4.     Compare the pos_qw of w with positions in va_pos_qw
  //          and for the position which is equal set the appropriate
  //          bit in va vector.
  //   5.   Set va_pos_qw to the next qw positions of "r"
  __m512i  va[NUM_ZMMS], va_pos_qw[NUM_ZMMS];
  __m512i  w_pos_qw, w_pos_bit, one, inc;
  __mmask8 va_mask;

  uint64_t *r64 = (uint64_t *)r;

  one = SET1_I64(1);
  inc = SET1_I64(QWORDS_IN_ZMM);

  // 1. Initialize
  va_pos_qw[0] = SET_I64(7, 6, 5, 4, 3, 2, 1, 0);
  for(size_t i = 1; i < NUM_ZMMS; i++) {
    va_pos_qw[i] = ADD_I64(va_pos_qw[i - 1], inc);
  }

  // va_pos_qw vectors hold qw positions 0 .. (NUM_ZMMS * QWORDS_IN_ZMM - 1)
  // Therefore, we set the increment vector inc such that by adding it to
  // va_pos_qw vectors they hold the next ZMMS_QWORDS qw positions.
  inc = SET1_I64(ZMMS_QWORDS);

  for(size_t i = 0; i < (sizeof(*r) / sizeof(uint64_t)); i += ZMMS_QWORDS) {
    for(size_t va_iter = 0; va_iter < NUM_ZMMS; va_iter++) {
      va[va_iter] = SET_ZERO;
    }

    for(size_t w_iter = 0; w_iter < w_size; w_iter++) {
      int32_t w = wlist[w_iter] - first_pos;
      w_pos_qw  = SET1_I64(w >> 6);
      w_pos_bit = SLLI_I64(one, w & MASK(6));

      // 4. Compare the positions in va_pos_qw with w_pos_qw
      //    and set the appropriate bit in va
      for(size_t va_iter = 0; va_iter < NUM_ZMMS; va_iter++) {
        va_mask     = CMPMEQ_I64(va_pos_qw[va_iter], w_pos_qw);
        va[va_iter] = MOR_I64(va[va_iter], va_mask, va[va_iter], w_pos_bit);
      }
    }

    // 5. Set the va_pos_qw to the next qw positions of r
    //    and store the previously computed data in r
    for(size_t va_iter = 0; va_iter < NUM_ZMMS; va_iter++) {
      STORE(&r64[i + (va_iter * QWORDS_IN_ZMM)], va[va_iter]);
      va_pos_qw[va_iter] = ADD_I64(va_pos_qw[va_iter], inc);
    }
  }
}

