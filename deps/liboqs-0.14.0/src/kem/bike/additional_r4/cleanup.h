/* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0"
 *
 * Written by Nir Drucker, Shay Gueron and Dusan Kostic,
 * AWS Cryptographic Algorithms Group.
 */

#pragma once

#include <oqs/common.h>

#include "utilities.h"

/* Runs _thecleanup function on _thealloc once _thealloc went out of scope */
#define DEFER_CLEANUP(_thealloc, _thecleanup) \
  __attribute__((cleanup(_thecleanup))) _thealloc

/* Create a cleanup function for pointers from function func, which accepts a
 * pointer. This is useful for DEFER_CLEANUP, as it passes &_thealloc into
 * _thecleanup function. This way,
 * if _thealloc is a pointer _thecleanup would receive a pointer to a
 * pointer.*/
#define DEFINE_POINTER_CLEANUP_FUNC(type, func) \
  static inline void func##_pointer((type)*p)   \
  {                                             \
    if(p && *p) {                               \
      func(*p);                                 \
    }                                           \
  }                                             \
  struct __useless_struct_to_allow_trailing_semicolon__

// len is bytes length of in
#define secure_clean OQS_MEM_cleanse

#define CLEANUP_FUNC(name, type)               \
  _INLINE_ void name##_cleanup(IN OUT type *o) \
  {                                            \
    secure_clean((uint8_t *)o, sizeof(*o));    \
  }

CLEANUP_FUNC(r, r_t)
CLEANUP_FUNC(m, m_t)
CLEANUP_FUNC(e, e_t)
CLEANUP_FUNC(sk, sk_t)
CLEANUP_FUNC(ss, ss_t)
CLEANUP_FUNC(ct, ct_t)
CLEANUP_FUNC(pad_r, pad_r_t)
CLEANUP_FUNC(pad_e, pad_e_t)
CLEANUP_FUNC(seed, seed_t)
CLEANUP_FUNC(syndrome, syndrome_t)
CLEANUP_FUNC(upc, upc_t)
CLEANUP_FUNC(func_k, func_k_t)
CLEANUP_FUNC(dbl_pad_r, dbl_pad_r_t)

// The functions below require special handling because we deal
// with arrays and not structures.

_INLINE_ void compressed_idx_d_ar_cleanup(IN OUT compressed_idx_d_ar_t *o)
{
  for(int i = 0; i < N0; i++) {
    secure_clean((uint8_t *)&(*o)[i], sizeof((*o)[0]));
  }
}

_INLINE_ void seeds_cleanup(IN OUT seeds_t *o)
{
  for(int i = 0; i < NUM_OF_SEEDS; i++) {
    seed_cleanup(&(o->seed[i]));
  }
}
