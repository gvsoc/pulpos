// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP cluster driver private definitions, shared between the FC-side driver
// (cluster.c) and the cluster boot trampoline (cluster_entry.S). Keep this file
// C-preprocessor friendly: the .S includes it for the stack layout.

#pragma once

#include <arch/pulp/drivers/cluster_eu.h>

// Number of cores in the cluster. Core 0 is the controller (master): it runs
// the dispatched cluster tasks and masters the team forks, of which it is
// always a member. Cores 1..7 are the workers, waiting on the dispatch FIFO.
#define CLUSTER_NB_CORE            8
#define CLUSTER_NB_WORKERS         (CLUSTER_NB_CORE - 1)

// Controller core: CLUSTER_CONTROLLER_CORE (0), defined in cluster_eu.h.

// L1 TCDM window (cluster-absolute).
#define CLUSTER_L1_BASE            0x10000000
#define CLUSTER_L1_SIZE            0x10000
#define CLUSTER_L1_END             (CLUSTER_L1_BASE + CLUSTER_L1_SIZE)

// Boot stack at the top of TCDM. Only the controller (core 0) has one: it runs
// the cluster task entry in C and needs a full stack. The workers run a pure
// register idle loop (cluster_entry.S) with no boot stack at all and carve each
// fork's stack from the task scratch just before jumping to the entry.
#ifndef CLUSTER_CC_STACK_SIZE
#define CLUSTER_CC_STACK_SIZE      (1 << 11)   // 2 KB controller stack
#endif
#define CLUSTER_BOOT_STACKS_SIZE   CLUSTER_CC_STACK_SIZE

// Cluster L1 scratch region, reserved once at pi_cluster_open just below the
// controller boot stack. The worker fork stacks are carved from its top and the
// pi_cl_l1_scratch_alloc temporaries from its bottom.
#define CLUSTER_SCRATCH_TOP        (CLUSTER_L1_END - CLUSTER_BOOT_STACKS_SIZE)

// Cluster control unit (cluster_ctrl_v2).
#define CLUSTER_CTRL_BASE          0x10200000u
#define CLUSTER_CTRL_FETCH_EN      (CLUSTER_CTRL_BASE + 0x08u)
#define CLUSTER_CTRL_BOOTADDR(core) (CLUSTER_CTRL_BASE + 0x40u + 4u * (core))

// Hierarchical instruction cache control unit. The enable register is a mask:
// bits 0..NB_CORES-1 are the per-core L0 caches and the bit above them the
// shared L1.5 banks. It resets to 0, so the cluster fetches straight from the
// L2 until this is written.
#define CLUSTER_ICACHE_BASE        0x10201400u
#define CLUSTER_ICACHE_ENABLE      (CLUSTER_ICACHE_BASE + 0x00u)
#define CLUSTER_ICACHE_ENABLE_ALL  ((1u << (CLUSTER_NB_CORE + 1)) - 1u)

// FC interrupt the cluster raises (FC ITC software IRQ) to notify the fabric
// controller: a posted task has completed, or a cluster PE has a printf line
// ready to flush. A free FC software interrupt (0..7).
#define FC_IRQ_CLUSTER_NOTIF       0

// pi_cluster_job_t field byte offsets, used by the assembly controller loop
// (cluster_entry.S). Kept in sync with the struct by _Static_asserts in
// cluster.c.
#define PI_CLUSTER_JOB_ENTRY            0
#define PI_CLUSTER_JOB_ARG              4
#define PI_CLUSTER_JOB_SLAVE_STACK_SIZE 8
#define PI_CLUSTER_JOB_DONE             12
#define PI_CLUSTER_JOB_STATUS           16

#ifndef __ASSEMBLER__

// ITC status-set register of the FC: writing (1 << FC_IRQ_CLUSTER_NOTIF) there
// raises its notify IRQ (== pi_irq_set on the FC). Set at pi_cluster_open.
extern volatile uint32_t __pi_cluster_fc_notif_addr;

#include <stdint.h>

// Shared task slot in L2: the FC posts an entry/arg, wakes the controller with
// a software event, and waits; the controller runs it and raises an FC IRQ to
// report completion (see cluster.c / the FC notify handler).
typedef struct
{
    void (*volatile entry)(void *arg);
    void *volatile arg;
    volatile uint32_t slave_stack_size;  // FC -> controller: worker stack size
    volatile uint32_t done;      // controller -> FC: the job finished
    volatile int32_t status;     // controller -> FC: 0 ok, -1 not enough L1
} pi_cluster_job_t;

// Per-core boot entries (defined in cluster_entry.S).
extern void __pi_cluster_controller_entry(void);
extern void __pi_cluster_worker_entry(void);

// Shared task slot (defined in cluster.c).
extern pi_cluster_job_t __pi_cluster_job;

#endif
