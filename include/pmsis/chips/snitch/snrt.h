// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// Subset of the Snitch runtime (snRuntime, pulp-platform/snitch_cluster
// sw/runtime) API for the Snitch cluster port of PulpOS, with the same names
// and semantics, so that Snitch kernels written against snRuntime build
// unchanged: cluster core identification, hardware barrier, cycle counter,
// FPU fence and the SSR configuration helpers (from sw/runtime/src/ssr.h).
//
// The cluster has CONFIG_CLUSTER_NB_CORE cores: the compute cores come first,
// the last one is the data-mover (DMA) core. All of them run main() with the
// pulpos/multicore parameter (core 0 only by default).

#pragma once

#include <stdint.h>
#include <stddef.h>

//
// Cluster cores
//

static inline uint32_t snrt_hartid(void)
{
    uint32_t hartid;
    asm volatile("csrr %0, mhartid" : "=r"(hartid));
    return hartid;
}

static inline uint32_t snrt_cluster_core_num(void) { return CONFIG_CLUSTER_NB_CORE; }

static inline uint32_t snrt_cluster_core_idx(void)
{
    return snrt_hartid() % CONFIG_CLUSTER_NB_CORE;
}

static inline uint32_t snrt_cluster_compute_core_num(void)
{
    return CONFIG_CLUSTER_NB_CORE - 1;
}

static inline uint32_t snrt_cluster_dm_core_idx(void) { return CONFIG_CLUSTER_NB_CORE - 1; }

static inline int snrt_is_dm_core(void)
{
    return snrt_cluster_core_idx() == snrt_cluster_dm_core_idx();
}

static inline int snrt_is_compute_core(void) { return !snrt_is_dm_core(); }

// First free byte of the TCDM: the PulpOS port keeps the whole TCDM for the
// application, but the per-core stacks at its end (see the linker script)
static inline void *snrt_l1_next(void) { return (void *)CONFIG_CLUSTER_TCDM_BASE; }

//
// Synchronization
//

// Cluster hardware barrier: every cluster core must reach it.
static inline void snrt_cluster_hw_barrier(void)
{
    asm volatile("csrr x0, 0x7C2" ::: "memory");
}

//
// Cycle counter
//

static inline uint32_t snrt_mcycle(void)
{
    uint32_t r;
    asm volatile("csrr %0, mcycle" : "=r"(r) : : "memory");
    return r;
}

//
// FPU / SSR (from snRuntime sw/runtime/src/ssr.h)
//

// Synchronize the integer and float pipelines.
static inline void snrt_fpu_fence(void)
{
    unsigned tmp;
    asm volatile(
        "fmv.x.w %0, fa0\n"
        "mv      %0, %0\n"
        : "+r"(tmp)::"memory");
}

typedef enum {
    SNRT_SSR_DM0 = 0,
    SNRT_SSR_DM1 = 1,
    SNRT_SSR_DM2 = 2,
    SNRT_SSR_DM_ALL = 31
} snrt_ssr_dm_t;

typedef enum {
    SNRT_SSR_1D = 0,
    SNRT_SSR_2D = 1,
    SNRT_SSR_3D = 2,
    SNRT_SSR_4D = 3
} snrt_ssr_dim_t;

typedef enum {
    SNRT_SSR_REG_STATUS = 0,
    SNRT_SSR_REG_REPEAT = 1,
    SNRT_SSR_REG_BOUNDS = 2,
    SNRT_SSR_REG_STRIDES = 6,
    SNRT_SSR_REG_IDX_CFG = 10,
    SNRT_SSR_REG_IDX_BASE = 11,
    SNRT_SSR_REG_RPTR_INDIR = 16,
    SNRT_SSR_REG_RPTR = 24,
    SNRT_SSR_REG_WPTR = 28
} snrt_ssr_reg_t;

// Enable all SSRs (ssr CSR, 0x7C0).
static inline void snrt_ssr_enable(void) { asm volatile("csrsi 0x7C0, 1\n"); }

// Disable all SSRs.
static inline void snrt_ssr_disable(void) { asm volatile("csrci 0x7C0, 1\n"); }

// The register and SSR indexes are immediates: the callers must be inlined
// with constant arguments.
static inline __attribute__((always_inline)) uint32_t read_ssr_cfg(
    const snrt_ssr_reg_t reg, const snrt_ssr_dm_t dm)
{
    uint32_t value;
    asm volatile("scfgri %[value], %[dm] | %[reg]<<5\n"
                 : [ value ] "=r"(value)
                 : [ dm ] "i"(dm), [ reg ] "i"(reg));
    return value;
}

static inline __attribute__((always_inline)) void write_ssr_cfg(
    const snrt_ssr_reg_t reg, const snrt_ssr_dm_t dm, uint32_t value)
{
    asm volatile("scfgwi %[value], %[dm] | %[reg]<<5\n" ::[value] "r"(value),
                 [ dm ] "i"(dm), [ reg ] "i"(reg));
}

static inline __attribute__((always_inline)) void snrt_ssr_loop_1d(
    const snrt_ssr_dm_t dm, size_t b0, size_t s0)
{
    --b0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 0), dm, b0);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 0), dm, s0);
}

static inline __attribute__((always_inline)) void snrt_ssr_loop_2d(
    const snrt_ssr_dm_t dm, size_t b0, size_t b1, size_t s0, size_t s1)
{
    --b0;
    --b1;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 0), dm, b0);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 1), dm, b1);
    size_t a = 0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 0), dm, s0 - a);
    a += s0 * b0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 1), dm, s1 - a);
}

static inline __attribute__((always_inline)) void snrt_ssr_loop_3d(
    const snrt_ssr_dm_t dm, size_t b0, size_t b1, size_t b2, size_t s0,
    size_t s1, size_t s2)
{
    --b0;
    --b1;
    --b2;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 0), dm, b0);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 1), dm, b1);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 2), dm, b2);
    size_t a = 0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 0), dm, s0 - a);
    a += s0 * b0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 1), dm, s1 - a);
    a += s1 * b1;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 2), dm, s2 - a);
}

static inline __attribute__((always_inline)) void snrt_ssr_loop_4d(
    const snrt_ssr_dm_t dm, size_t b0, size_t b1, size_t b2, size_t b3,
    size_t s0, size_t s1, size_t s2, size_t s3)
{
    --b0;
    --b1;
    --b2;
    --b3;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 0), dm, b0);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 1), dm, b1);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 2), dm, b2);
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_BOUNDS + 3), dm, b3);
    size_t a = 0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 0), dm, s0 - a);
    a += s0 * b0;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 1), dm, s1 - a);
    a += s1 * b1;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 2), dm, s2 - a);
    a += s2 * b2;
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_STRIDES + 3), dm, s3 - a);
}

// Number of times each element is repeated (1 = no repetition).
static inline __attribute__((always_inline)) void snrt_ssr_repeat(
    const snrt_ssr_dm_t dm, size_t count)
{
    write_ssr_cfg(SNRT_SSR_REG_REPEAT, dm, count - 1);
}

// Start a streaming read.
static inline __attribute__((always_inline)) void snrt_ssr_read(
    const snrt_ssr_dm_t dm, const snrt_ssr_dim_t dim, volatile void *ptr)
{
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_RPTR + dim), dm, (uintptr_t)ptr);
}

// Start a streaming write.
static inline __attribute__((always_inline)) void snrt_ssr_write(
    const snrt_ssr_dm_t dm, const snrt_ssr_dim_t dim, volatile void *ptr)
{
    write_ssr_cfg((snrt_ssr_reg_t)(SNRT_SSR_REG_WPTR + dim), dm, (uintptr_t)ptr);
}
