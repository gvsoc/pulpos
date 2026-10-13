// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP cluster DMA (mchan) driver — callable from cluster code only.
//
// The DMA moves data between the cluster L1 TCDM (loc) and any address
// outside the cluster (ext, typically L2). A copy is enqueued with
// pi_cluster_dma_copy / pi_cluster_dma_copy_2d and joined with
// pi_cluster_dma_wait, which puts the calling core to sleep on the DMA
// end-of-transfer event:
//
//     pi_cluster_dma_copy_t copy;
//     pi_cluster_dma_copy((uint32_t)l2_buff, (uint32_t)l1_buff, size,
//         PI_CLUSTER_DMA_DIR_EXT2LOC, &copy);
//     // ... compute ...
//     pi_cluster_dma_wait(&copy);
//
// Each copy allocates one of the 16 hardware transfer ids (the allocation
// read stalls the core when all are busy); the wait frees it. The maximum
// size of a single copy is 128 KB - 1 (17-bit length).

#pragma once

#include <stdint.h>
#include <arch/pulp/drivers/cluster_eu.h>

// Transfer direction
typedef enum
{
    PI_CLUSTER_DMA_DIR_LOC2EXT = 0,  // cluster L1 -> external memory
    PI_CLUSTER_DMA_DIR_EXT2LOC = 1   // external memory -> cluster L1
} pi_cluster_dma_dir_e;

// One enqueued copy; holds the hardware transfer id between the enqueue and
// the wait.
typedef struct pi_cluster_dma_copy_s
{
    uint32_t id;
} pi_cluster_dma_copy_t;


// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

// Per-core demux channel of the cluster DMA: each PE reaches its own command
// queue here (demux_peripherals/dma in the GVSoC cluster description), right
// above the event-unit demux window.
#define __PI_CLUSTER_DMA_BASE       0x10204400u
#define __PI_CLUSTER_DMA_CMD        (__PI_CLUSTER_DMA_BASE + 0x0u)
#define __PI_CLUSTER_DMA_STATUS     (__PI_CLUSTER_DMA_BASE + 0x4u)

// CMD word 0 fields
#define __PI_CLUSTER_DMA_CMD_TYPE_BIT   17  // 0: loc->ext, 1: ext->loc
#define __PI_CLUSTER_DMA_CMD_INC_BIT    18  // incremental addressing
#define __PI_CLUSTER_DMA_CMD_2D_EXT_BIT 19  // 2D transfer on the ext side
#define __PI_CLUSTER_DMA_CMD_ELE_BIT    20  // raise event at end of transfer
#define __PI_CLUSTER_DMA_CMD_BLE_BIT    22  // broadcast event to all cores

// Reading CMD allocates a transfer id (stalls while none is free)
static inline uint32_t __pi_cluster_dma_alloc(void)
{
    return *(volatile uint32_t *)__PI_CLUSTER_DMA_CMD;
}

static inline uint32_t __pi_cluster_dma_cmd(pi_cluster_dma_dir_e dir,
                                            uint32_t size, uint32_t is_2d)
{
    return size |
        ((uint32_t)dir << __PI_CLUSTER_DMA_CMD_TYPE_BIT) |
        (1u << __PI_CLUSTER_DMA_CMD_INC_BIT) |
        (is_2d << __PI_CLUSTER_DMA_CMD_2D_EXT_BIT) |
        (1u << __PI_CLUSTER_DMA_CMD_ELE_BIT);
}

static inline void __pi_cluster_dma_push(uint32_t value)
{
    *(volatile uint32_t *)__PI_CLUSTER_DMA_CMD = value;
}


// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

// Enqueue a 1D copy of `size` bytes between `ext` and `loc`.
static inline void pi_cluster_dma_copy(uint32_t ext, uint32_t loc, uint32_t size,
    pi_cluster_dma_dir_e dir, pi_cluster_dma_copy_t *copy)
{
    // A NULL copy is a "merged" transfer (e.g. the autotiler's follow-up tiles):
    // it rides on the preceding copy's id and is freed by the wait on that copy.
    // Allocate an id only for non-merged copies — dereferencing a NULL copy
    // traps, and giving each merged copy its own id leaks the id space.
    if (copy) copy->id = __pi_cluster_dma_alloc();
    // Make sure the data the DMA will read is visible before the transfer
    // is pushed.
    __asm__ __volatile__("" ::: "memory");
    __pi_cluster_dma_push(__pi_cluster_dma_cmd(dir, size, 0));
    __pi_cluster_dma_push(loc);
    __pi_cluster_dma_push(ext);
}

// Enqueue a 2D copy: `size` bytes in total, walked on the ext side as lines
// of `length` bytes spaced by `stride` bytes. The loc side is linear.
static inline void pi_cluster_dma_copy_2d(uint32_t ext, uint32_t loc, uint32_t size,
    uint32_t stride, uint32_t length,
    pi_cluster_dma_dir_e dir, pi_cluster_dma_copy_t *copy)
{
    // See pi_cluster_dma_copy: only non-merged copies allocate an id.
    if (copy) copy->id = __pi_cluster_dma_alloc();
    __asm__ __volatile__("" ::: "memory");
    __pi_cluster_dma_push(__pi_cluster_dma_cmd(dir, size, 1));
    __pi_cluster_dma_push(loc);
    __pi_cluster_dma_push(ext);
    __pi_cluster_dma_push(length);
    __pi_cluster_dma_push(stride);
}

// Block until the copy completed, sleeping on the DMA end-of-transfer event,
// then release its transfer id.
static inline void pi_cluster_dma_wait(pi_cluster_dma_copy_t *copy)
{
    while (*(volatile uint32_t *)__PI_CLUSTER_DMA_STATUS & (1u << copy->id))
    {
        cluster_eu_evt_mask_wait_clear(1u << CLUSTER_EU_DMA0_EVENT);
    }

    // Free the transfer id
    *(volatile uint32_t *)__PI_CLUSTER_DMA_STATUS = 1u << copy->id;
}

// Wait for the copy without sleeping the core: same completion as
// pi_cluster_dma_wait, polled. For measurements which count active cycles.
static inline void pi_cluster_dma_busy_wait(pi_cluster_dma_copy_t *copy)
{
    while (*(volatile uint32_t *)__PI_CLUSTER_DMA_STATUS & (1u << copy->id))
        ;
    *(volatile uint32_t *)__PI_CLUSTER_DMA_STATUS = 1u << copy->id;
}
