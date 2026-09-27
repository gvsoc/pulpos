// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// SoftHier hardware access for applications: position of the core, remote
// TCDM addresses and synchronization. Works on both SoftHier generations
// (pulp.chips.softhier and pulp.chips.softhier_v2), which have the same
// software interface. The CONFIG_SOFTHIER_* values come from the board
// attributes (see python/pulpos/softhier.py).

#pragma once

#include <stdint.h>

// Cluster control registers: cluster ID, and the per-cluster character output
#define SOFTHIER_CLUSTER_REG_ID      (CONFIG_SOFTHIER_CLUSTER_REG_BASE + 0x0)
#define SOFTHIER_CLUSTER_REG_PUTC    (CONFIG_SOFTHIER_CLUSTER_REG_BASE + 0xC)

// SoC control registers
#define SOFTHIER_SOC_REG_EOC         (CONFIG_SOFTHIER_SOC_REG_BASE + 0x0)
#define SOFTHIER_SOC_REG_EOC_ALL     (CONFIG_SOFTHIER_SOC_REG_BASE + 0x4)

// The global barrier counters, at the top of the TCDM of cluster 0. The
// linker script keeps the top of the TCDM out of the .l1 section.
#define SOFTHIER_GLOBAL_BARRIER_OFFSET      (CONFIG_SOFTHIER_TCDM_SIZE - 8)
#define SOFTHIER_GLOBAL_BARRIER_ITER_OFFSET (CONFIG_SOFTHIER_TCDM_SIZE - 16)
// Lock of the character output of the cluster, in the local TCDM, taken while
// a core writes a line (multicore)
#define SOFTHIER_PUTC_LOCK_OFFSET           (CONFIG_SOFTHIER_TCDM_SIZE - 24)

static inline uint32_t pi_softhier_cluster_id()
{
    return *(volatile uint32_t *)SOFTHIER_CLUSTER_REG_ID;
}

static inline uint32_t pi_softhier_nb_cluster()
{
    return CONFIG_SOFTHIER_NB_CLUSTER;
}

static inline uint32_t pi_softhier_core_id()
{
    uint32_t hartid;
    __asm__ volatile("csrr %0, mhartid" : "=r"(hartid));
    return hartid;
}

static inline uint32_t pi_softhier_nb_core()
{
    return CONFIG_SOFTHIER_NB_CORE_PER_CLUSTER;
}

// Address of a local TCDM location
static inline uintptr_t pi_softhier_tcdm(uint32_t offset)
{
    return CONFIG_SOFTHIER_TCDM_BASE + offset;
}

// Address of a location in the TCDM of cluster `cid`, through the NoC
static inline uintptr_t pi_softhier_remote_tcdm(uint32_t cid, uint32_t offset)
{
    return CONFIG_SOFTHIER_TCDM_REMOTE + cid * CONFIG_SOFTHIER_TCDM_SIZE + offset;
}

// Hardware barrier between the cores of the cluster: returns once every core
// of the cluster called it
static inline void pi_softhier_cluster_barrier()
{
    __asm__ volatile("csrr x0, 0x7C2" ::: "memory");
}

// Barrier between all the cores of all the clusters
void pi_softhier_global_barrier();

// Report the end of computation of this core with its status. The
// simulation stops once every core of every cluster reported, with the OR of
// the statuses as exit status.
static inline void pi_softhier_eoc(uint32_t status)
{
    *(volatile uint32_t *)SOFTHIER_SOC_REG_EOC_ALL = status;
}

// iDMA of the cluster, driven by the Xdma instructions of the last core of
// the cluster (the one bound to the iDMA): the other cores must not call
// these. Addresses are the ones seen by the cluster (local or remote TCDM).
// The instructions are encoded by hand, the compiler does not know Xdma.

#define SOFTHIER_XDMA_ENCODE(funct7, rs2, rs1, rd) \
    (((funct7) << 25) | ((rs2) << 20) | ((rs1) << 15) | ((rd) << 7) | 0b0101011)

static inline int pi_softhier_is_dma_core()
{
    return pi_softhier_core_id() == CONFIG_SOFTHIER_NB_CORE_PER_CLUSTER - 1;
}

// Start a 1D transfer of `size` bytes, returns the transfer ID
static inline uint32_t pi_softhier_dma_1d(uintptr_t dst, uintptr_t src, uint32_t size)
{
    register uint32_t a0 __asm__("a0") = dst;
    register uint32_t a1 __asm__("a1") = 0;
    register uint32_t a2 __asm__("a2") = src;
    register uint32_t a3 __asm__("a3") = 0;
    register uint32_t a4 __asm__("a4") = size;

    // dmsrc a2, a3 / dmdst a0, a1 / dmcpyi a0, a4, 0
    __asm__ volatile(".word %0" :: "i"(SOFTHIER_XDMA_ENCODE(0, 13, 12, 0)), "r"(a2), "r"(a3));
    __asm__ volatile(".word %0" :: "i"(SOFTHIER_XDMA_ENCODE(1, 11, 10, 0)), "r"(a0), "r"(a1));
    __asm__ volatile(".word %1" : "=r"(a0) : "i"(SOFTHIER_XDMA_ENCODE(2, 0, 14, 10)), "r"(a4)
        : "memory");
    return a0;
}

// Start a 2D transfer of `repeat` rows of `size` bytes, returns the transfer
// ID
static inline uint32_t pi_softhier_dma_2d(uintptr_t dst, uintptr_t src, uint32_t size,
    uint32_t dst_stride, uint32_t src_stride, uint32_t repeat)
{
    register uint32_t a0 __asm__("a0") = dst;
    register uint32_t a1 __asm__("a1") = 0;
    register uint32_t a2 __asm__("a2") = src;
    register uint32_t a3 __asm__("a3") = 0;
    register uint32_t a4 __asm__("a4") = size;
    register uint32_t a5 __asm__("a5") = dst_stride;
    register uint32_t a6 __asm__("a6") = src_stride;
    register uint32_t a7 __asm__("a7") = repeat;

    // dmsrc a2, a3 / dmdst a0, a1 / dmstr a6, a5 / dmrep a7 / dmcpyi a0, a4, 2
    __asm__ volatile(".word %0" :: "i"(SOFTHIER_XDMA_ENCODE(0, 13, 12, 0)), "r"(a2), "r"(a3));
    __asm__ volatile(".word %0" :: "i"(SOFTHIER_XDMA_ENCODE(1, 11, 10, 0)), "r"(a0), "r"(a1));
    __asm__ volatile(".word %0" :: "i"(SOFTHIER_XDMA_ENCODE(6, 15, 16, 0)), "r"(a6), "r"(a5));
    __asm__ volatile(".word %0" :: "i"(SOFTHIER_XDMA_ENCODE(7, 0, 17, 0)), "r"(a7));
    __asm__ volatile(".word %1" : "=r"(a0) : "i"(SOFTHIER_XDMA_ENCODE(2, 2, 14, 10)), "r"(a4)
        : "memory");
    return a0;
}

// Wait until the iDMA has no transfer left
static inline void pi_softhier_dma_wait_all()
{
    // dmstati t0, 2 (busy status)
    __asm__ volatile(
        "1: .word %0\n"
        "bnez t0, 1b" :: "i"(SOFTHIER_XDMA_ENCODE(4, 2, 0, 5)) : "t0", "memory");
}
