// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP compute-cluster driver (public API), for pulp_open:
//   * pi_cluster_open  -> boot the cluster controller core and the workers,
//   * pi_cluster_send_task_to_cl -> run an entry point on the cluster and wait
//                         for it to finish,
//   * pi_cluster_close -> stop using the cluster, its cores stay asleep.
//
// The parallel team API (pi_cl_team_fork / barrier) is layered on top of the
// hardware event unit (see the Team API section below).

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <pmsis/kernel/kernel.h>
#include <pmsis/kernel/event.h>
// Event-unit helpers backing the inline team barrier below (hardware barrier 0).
#include <arch/pulp/drivers/cluster_eu.h>

// Cluster configuration. Embedded in the cluster device instantiated by the
// chip BSP (PI_BSP_CLUSTER_INST, see arch/pulp/bsp/pulp_open.c); applications do
// not build it.
typedef struct pi_cluster_conf
{
    int cid;                  // cluster id (only 0 exists on pulp_open)
    // Size in bytes of the L1 scratch region reserved once at pi_cluster_open
    // (worker fork stacks grow down from its top, pi_cl_l1_scratch_alloc grows up
    // from its bottom). 0 = PI_CL_DEFAULT_SCRATCH_SIZE.
    // Can be changed before opening with pi_cluster_scratch_size_set.
    unsigned int scratch_size;
} pi_cluster_conf_t;

// Cluster device. Instantiated by the chip BSP, applications get it with
// PI_BSP_CLUSTER(id) from <pmsis/bsp/bsp.h> and pass it as a pi_device_t.
typedef struct pi_cluster
{
    pi_cluster_conf_t conf;
    int open_count;
} pi_cluster_t;

// Number of cores in the cluster. Core 0, the controller, runs the cluster
// tasks and is a member of every fork; cores 1..7 are the workers, whose stacks
// are carved from the L1 scratch. Last is the default worker stack size used
// when a task is created.
#define PI_CL_NB_CORE                   8
#define PI_CL_NB_PE                     PI_CL_NB_CORE
#define PI_CL_DEFAULT_SLAVE_STACK_SIZE  1024

// Default size of the cluster L1 scratch region (covers the default task,
// 7 workers * default slave stack = 7 KiB, with headroom).
#define PI_CL_DEFAULT_SCRATCH_SIZE      (16 * 1024)

// Board-support macros, used by the chip BSP to declare the cluster device with
// its configuration filled at compile time (the classic PulpOS pattern).
#define PI_BSP_CLUSTER_CONF(user_cid)                 \
{                                                     \
    .cid          = (user_cid),                       \
    .scratch_size = PI_CL_DEFAULT_SCRATCH_SIZE,       \
}

#define PI_BSP_CLUSTER_INST(instance_name, user_conf) \
pi_cluster_t instance_name = {                        \
    .conf = user_conf,                                \
}

#define PI_BSP_CLUSTER_DECL(instance_name) \
extern pi_cluster_t instance_name

// Place a global in cluster L1 TCDM. L1 is not preloaded by the loader, so the
// variable's init image is embedded in L2 and copied into L1 on every
// pi_cluster_open. Keep such globals small (the image is recopied on each
// open). An uninitialized global tagged with this macro is emitted zero-filled
// and still occupies space in the L2 image.
//
//   PI_CL_L1_GLOBAL int coeffs[4] = { 1, 2, 3, 4 };
#define PI_CL_L1_GLOBAL  __attribute__((section(".l1_data")))

// A piece of work to run on the cluster: an entry point and its argument.
typedef struct pi_cluster_task
{
    void (*entry)(void *arg);     // runs on the cluster controller core
    void *arg;                    // passed to entry
    struct pi_cluster_task *next; // reserved for future queuing
    // Worker stack size (per core). Worker stacks are carved from the cluster L1
    // scratch region (reserved once at open; see pi_cl_l1_scratch_alloc).
    unsigned int slave_stack_size;
    int status;                   // completion status (0 ok, -1 not enough L1)
} pi_cluster_task_t;

// Change the size of the L1 scratch region reserved at the next open (see
// pi_cluster_conf_t.scratch_size). Returns -1 if the cluster is already open.
int pi_cluster_scratch_size_set(pi_device_t *device, unsigned int size);

// Boot the cluster cores, using the configuration
// embedded in the BSP device. Returns 0 on success. Nested opens are
// reference-counted.
int pi_cluster_open(pi_device_t *device);

// Stop using the cluster (when the last opener closes it). Its cores stay
// asleep, waiting for the next open.
void pi_cluster_close(pi_device_t *device);

// Initialise a cluster task in place and return it (convenience helper). The
// worker stack size defaults to PI_CL_DEFAULT_SLAVE_STACK_SIZE. The cluster L1
// scratch is a single region reserved once at pi_cluster_open, sized by
// the device scratch_size (shared by the worker stacks and pi_cl_l1_scratch_alloc);
// override the per-fork worker stack size with pi_cluster_task_stacks() before
// sending.
static inline pi_cluster_task_t *pi_cluster_task(pi_cluster_task_t *task,
                                                 void (*entry)(void *), void *arg)
{
    task->entry = entry;
    task->arg = arg;
    task->next = NULL;
    task->slave_stack_size = PI_CL_DEFAULT_SLAVE_STACK_SIZE;
    task->status = 0;
    return task;
}

// Set the worker stack size for the task (bytes per core). Worker stacks are
// carved from the top of the cluster scratch region (reserved once at open),
// so (PI_CL_NB_CORE - 1) * stack_size must fit it or the task fails with
// status -1.
static inline pi_cluster_task_t *pi_cluster_task_stacks(pi_cluster_task_t *task,
                                                        unsigned int stack_size)
{
    task->slave_stack_size = stack_size;
    return task;
}

// Run `task` on the cluster and block until it completes.
int pi_cluster_send_task_to_cl(pi_device_t *device, pi_cluster_task_t *task);

// Run `task` on the cluster; `event` is notified when it completes.
int pi_cluster_send_task_to_cl_async(pi_device_t *device,
                                     pi_cluster_task_t *task, pi_evt_t *event);

// Post `event` to the FC from a cluster task (controller only). Blocks on the
// cluster side until any previous CL→FC event has been consumed (only one
// in flight at a time). The FC notifies `event` once it dequeues it.
void pi_cl_send_task_to_fc(pi_evt_t *event);


// ---------------------------------------------------------------------------
// Team API — callable from cluster code only (inside a cluster task).
// ---------------------------------------------------------------------------

// Run `entry(arg)` on cores 0..nb_cores-1 in parallel (0 = all cores), the
// calling controller included. Must be called from the cluster controller (the
// core running the cluster task). Returns once every team core has finished
// (hardware barrier). Forks do not nest.
void pi_cl_team_fork(int nb_cores, void (*entry)(void *arg), void *arg);

// Synchronize the cores of the current team (hardware barrier 0). Callable
// from any team core inside the forked entry. Inlined to a single p.elw
// event-load (no call overhead on this hot synchronization path).
static inline void pi_cl_team_barrier(void)
{
    cluster_eu_barrier_trigger_wait_clear();
}

// Number of cores in the current team.
int pi_cl_team_nb_cores(void);

// Change the worker stack size for subsequent forks (bytes per core). Callable
// from the cluster controller (the task entry), between forks. The new size must
// keep (PI_CL_NB_CORE - 1) * size within the task's reserved scratch (minus any live
// scratch_alloc); it is clamped to the largest size that still fits.
void pi_cl_team_set_stack_size(unsigned int size);

// ---------------------------------------------------------------------------
// Cluster L1 scratch allocator — a fast linear (bump) allocator over the task's
// reserved scratch area, for short-lived working buffers. Controller-context
// only: the controller is single-threaded with no interrupt handlers, so the
// bump cursor needs no lock; do not call concurrently from a fork body. Frees
// must be LIFO.
// ---------------------------------------------------------------------------

// Allocate `size` bytes (8-byte aligned) from the current task's scratch area.
// Returns NULL if it would collide with the worker stacks at the top.
void *pi_cl_l1_scratch_alloc(size_t size);

// Release the last `size` bytes allocated with pi_cl_l1_scratch_alloc (LIFO).
void pi_cl_l1_scratch_free(size_t size);

// Id of the calling core within the cluster (0..7 on cluster cores).
static inline int pi_core_id(void)
{
    int hartid;
    __asm__ volatile ("csrr %0, mhartid" : "=r" (hartid));
    return hartid & 0x1f;
}

// Id of the calling core's cluster (0 on pulp_open's single cluster).
static inline int pi_cluster_id(void)
{
    int hartid;
    __asm__ volatile ("csrr %0, mhartid" : "=r" (hartid));
    return (hartid >> 5) & 0x3f;
}

// ---------------------------------------------------------------------------
// Cluster cycle counter — callable from cluster code only. Backed by the
// cluster timer in 64-bit cascaded mode, clocked by the cluster clock (the
// cores have no mcycle CSR). pi_cluster_open leaves it stopped at 0; it counts
// between pi_cl_cycle_start and pi_cl_cycle_stop, also while the cores sleep.
// ---------------------------------------------------------------------------

#define PI_CL_TIMER_BASE            0x10200400u
#define PI_CL_TIMER_CFG_LO          0x00u
#define PI_CL_TIMER_CNT_LO          0x08u
#define PI_CL_TIMER_CNT_HI          0x0Cu
#define PI_CL_TIMER_RESET_LO        0x20u
#define PI_CL_TIMER_RESET_HI        0x24u
#define PI_CL_TIMER_CFG_LO_ENABLE   (1u << 0)
#define PI_CL_TIMER_CFG_LO_CASC     (1u << 31)
#define PI_CL_TIMER_REG(offset)     (*(volatile uint32_t *)(PI_CL_TIMER_BASE + (offset)))

// Start (or resume) counting, without clearing the counter.
static inline __attribute__((always_inline)) void pi_cl_cycle_start(void)
{
    PI_CL_TIMER_REG(PI_CL_TIMER_CFG_LO) = PI_CL_TIMER_CFG_LO_CASC | PI_CL_TIMER_CFG_LO_ENABLE;
}

// Stop counting. The counter keeps its value.
static inline __attribute__((always_inline)) void pi_cl_cycle_stop(void)
{
    PI_CL_TIMER_REG(PI_CL_TIMER_CFG_LO) = PI_CL_TIMER_CFG_LO_CASC;
}

// Clear the 64-bit counter. It keeps counting if it was started.
static inline __attribute__((always_inline)) void pi_cl_cycle_reset(void)
{
    PI_CL_TIMER_REG(PI_CL_TIMER_RESET_LO) = 1;
    PI_CL_TIMER_REG(PI_CL_TIMER_RESET_HI) = 1;
}

// Low 32 bits of the counter.
static inline __attribute__((always_inline)) uint32_t pi_cl_cycle_get32(void)
{
    return PI_CL_TIMER_REG(PI_CL_TIMER_CNT_LO);
}

// Full 64-bit counter. The high word is read again to catch a carry from the
// low word between the two reads.
static inline __attribute__((always_inline)) uint64_t pi_cl_cycle_get64(void)
{
    uint32_t hi, lo;
    do
    {
        hi = PI_CL_TIMER_REG(PI_CL_TIMER_CNT_HI);
        lo = PI_CL_TIMER_REG(PI_CL_TIMER_CNT_LO);
    } while (hi != PI_CL_TIMER_REG(PI_CL_TIMER_CNT_HI));
    return ((uint64_t)hi << 32) | lo;
}

static inline __attribute__((always_inline)) uint32_t pi_cl_cycle_get(void)
{
    return pi_cl_cycle_get32();
}

// Cluster DMA (callable from cluster code only).
#include <pmsis/chips/pulp/cluster_dma.h>
