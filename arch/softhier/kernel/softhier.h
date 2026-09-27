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
