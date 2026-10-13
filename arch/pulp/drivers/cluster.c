// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP compute-cluster driver (pulp_open). The controller core (core 0) and
// the workers (cores 1..7) boot to distinct entry points and both sleep on the event unit, and
// the cluster reports back to the FC through a fabric software interrupt rather
// than a polled mailbox.
//
// Bring-up (pi_cluster_open): the cluster is always powered and clocked on
// pulp_open, so opening it only initializes the L1 globals, registers the FC
// notify IRQ, enables the instruction cache and, on the first open, boots the
// PEs (controller -> the EU task wait-loop, workers -> the dispatch-FIFO
// wait-loop). Closing it leaves the PEs asleep in these loops, clock-gated by
// the event unit, where the next open finds them. They are not halted: a core
// halted while parked on an event-unit read would leave that read pending in
// the event unit, which then answers it after the core has rebooted.
//
// Task dispatch is event-driven: pi_cluster_send_task_to_cl posts the entry in
// a shared slot, raises a software event on the controller's event unit to wake
// it, and sleeps on a completion event. The controller runs the task then
// raises the FC notify IRQ; the FC handler reports completion and also flushes
// any cluster-PE printf lines (cluster cores cannot do semihosting themselves).
// The parallel team API (pi_cl_team_fork / barrier) runs on the event unit: the
// dispatch FIFO feeds entry+arg to the workers and the hardware barriers join
// the team.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pmsis/kernel/event.h>
#include <pmsis/kernel/irq.h>
#include <pmsis/chips/pulp/cluster.h>
#include PI_CHIP_INC(CONFIG_CHIP_FAMILY_NAME, kernel/hal.h)
#include "cluster_implem.h"
#include "cluster_eu.h"

// Cluster L1 init-data image (linker, see link.ld / the .l1_data section).
// Globals tagged PI_CL_L1_GLOBAL live in L1 (start..end) but their init image is
// stored in L2 (load); the FC copies load -> start on every open since L1 is not
// preloaded.
extern uint8_t __cluster_l1_data_start[];
extern uint8_t __cluster_l1_data_end[];
extern uint8_t __cluster_l1_data_load[];

// Shared task slot in L2 (FC .bss). Single cluster -> a single instance. Not
// static: the assembly controller loop (cluster_entry.S) loads job.arg from it.
pi_cluster_job_t __pi_cluster_job;
_Static_assert(offsetof(pi_cluster_job_t, arg) == PI_CLUSTER_JOB_ARG,
               "PI_CLUSTER_JOB_ARG (cluster_implem.h) out of sync with the struct");

// Event the FC is waiting on for the running task, notified from the FC notify
// IRQ handler when the controller reports completion. Single cluster -> one
// task in flight at a time. The matching task pointer is kept so the handler can
// surface the completion status (used by the async path).
static pi_evt_t *volatile __pi_cluster_task_event;
static pi_cluster_task_t *volatile __pi_cluster_task;

// Shared slot for a cluster-to-FC event (pi_cl_send_task_to_fc). NULL means
// the slot is free. Lives in L2 so both the cluster and the FC can access it.
// Protected by EU mutex 0 on the cluster side; the FC handler clears it from
// the FC notify IRQ context after notifying the posted event.
volatile pi_evt_t *__pi_cl_to_fc_event;

// ITC status-set register of the FC core which opened the cluster, the one
// getting its notifications. Not static: the controller loop loads it.
volatile uint32_t __pi_cluster_fc_notif_addr;

// Set once the PEs have been booted, they then stay in their wait loops
static int __pi_cluster_booted;

// Current team size, set by the fork master before waking the workers.
// Read by pi_cl_team_nb_cores() from any team core during the fork.
volatile int __pi_cluster_team_nb_cores = 1;

// Cluster L1 scratch region, reserved once at pi_cluster_open (base/size/top
// are fixed for the cluster's lifetime; see CLUSTER_SCRATCH_* in
// cluster_implem.h). The asm worker loop (cluster_entry.S) reads stacks_top and
// stack_size to place each worker's stack (top = stacks_top - core_id*stack_size);
// pi_cl_l1_scratch_alloc bump-allocates from scratch_base upward. Per task the
// controller only sets stack_size and clears scratch_used. Not static: the asm
// references stacks_top/stack_size by symbol.
volatile uintptr_t __pi_cluster_stacks_top;     // top of the reserved scratch
volatile uint32_t  __pi_cluster_stack_size;     // current worker stack size
volatile uintptr_t __pi_cluster_scratch_base;
volatile uint32_t  __pi_cluster_scratch_size;   // reserved scratch bytes
volatile uint32_t  __pi_cluster_scratch_used;   // scratch_alloc high-water mark


// ---------------------------------------------------------------------------
// Cluster side: controller (core 0) and workers (cores 1..7).
// ---------------------------------------------------------------------------

// Round up to a multiple of n (n a power of two).
static inline uint32_t __pi_round_up(uint32_t v, uint32_t n)
{
    return (v + (n - 1)) & ~(n - 1);
}

// The controller task wait-loop lives entirely in cluster_entry.S
// (__pi_cluster_controller_entry): a p.elw event-wait with the loop invariants
// in registers and a frameless dispatch to the task entry. The scratch is reserved once at open, so the loop allocates nothing —
// it just sets the per-task worker stack size and reports completion by writing
// the FC ITC register itself. The constants it hardcodes are kept in sync with
// the C definitions here:
_Static_assert(offsetof(pi_cluster_job_t, entry) == PI_CLUSTER_JOB_ENTRY, "");
_Static_assert(offsetof(pi_cluster_job_t, slave_stack_size) == PI_CLUSTER_JOB_SLAVE_STACK_SIZE, "");
_Static_assert(offsetof(pi_cluster_job_t, done) == PI_CLUSTER_JOB_DONE, "");
_Static_assert(offsetof(pi_cluster_job_t, status) == PI_CLUSTER_JOB_STATUS, "");

// The worker idle loop lives in cluster_entry.S (__pi_cluster_worker_entry): it
// runs entirely in registers and relocates each fork onto a per-core stack
// carved from the task's scratch region, so it cannot be expressed in C.


// ---------------------------------------------------------------------------
// Cluster side: team API (called from a cluster task on the controller, and
// from the forked entry on any team core for the barrier).
// ---------------------------------------------------------------------------

void pi_cl_team_fork(int nb_cores, void (*entry)(void *), void *arg)
{
    if (nb_cores == 0 || nb_cores > CLUSTER_NB_CORE)
        nb_cores = CLUSTER_NB_CORE;

    // The team is cores 0..nb_cores-1, the master (core 0) included. Barrier
    // 0 joins all of them, for pi_cl_team_barrier and for the closing
    // rendez-vous below.
    uint32_t team_mask = (1u << nb_cores) - 1;
    uint32_t worker_mask = team_mask & ~(1u << CLUSTER_CONTROLLER_CORE);

    __pi_cluster_team_nb_cores = nb_cores;

    cluster_eu_barrier_setup(team_mask);

    if (worker_mask)
    {
        // Workers pop entry then arg from the dispatch FIFO. Make sure the team
        // globals are visible before the workers wake.
        __asm__ volatile("" ::: "memory");
        cluster_eu_dispatch_team_config(worker_mask);
        cluster_eu_dispatch_push((uint32_t)(uintptr_t)entry);
        cluster_eu_dispatch_push((uint32_t)(uintptr_t)arg);
    }

    entry(arg);

    // Closing rendez-vous: the master waits on the barrier for the workers.
    cluster_eu_barrier_trigger_wait_clear();
}

// pi_cl_team_barrier() is now a static inline in cluster.h (single p.elw,
// no call overhead on the hot path).

int pi_cl_team_nb_cores(void)
{
    return __pi_cluster_team_nb_cores;
}


// ---------------------------------------------------------------------------
// Cluster side: L1 scratch allocator + dynamic worker stack size. Controller
// context only: the controller runs single-threaded with no interrupt handlers,
// so the bump cursor needs no locking. The worker stacks live at the top of the
// reserved region and the scratch_alloc temporaries at the bottom.
// ---------------------------------------------------------------------------

void pi_cl_team_set_stack_size(unsigned int size)
{
    size = __pi_round_up(size, 16);

    // Keep the worker stacks (NB_WORKERS * size, carved from the top) clear of the
    // live scratch_alloc high-water mark; clamp to the largest size that fits.
    uint32_t avail = __pi_cluster_scratch_size - __pi_cluster_scratch_used;
    uint32_t max_size = (avail / CLUSTER_NB_WORKERS) & ~15u;
    if (size > max_size)
        size = max_size;

    __pi_cluster_stack_size = size;
}

void *pi_cl_l1_scratch_alloc(size_t size)
{
    size = __pi_round_up((uint32_t)size, 8);

    // Ceiling: stay below the worker-stack region (NB_WORKERS * stack_size) at the
    // top of the reserved scratch.
    uint32_t ceiling = __pi_cluster_scratch_size
                       - CLUSTER_NB_WORKERS * __pi_cluster_stack_size;
    void *p = NULL;
    if (__pi_cluster_scratch_used + size <= ceiling)
    {
        p = (void *)(__pi_cluster_scratch_base + __pi_cluster_scratch_used);
        __pi_cluster_scratch_used += size;
    }
    return p;
}

void pi_cl_l1_scratch_free(size_t size)
{
    size = __pi_round_up((uint32_t)size, 8);

    if (__pi_cluster_scratch_used >= size)
        __pi_cluster_scratch_used -= size;
}


// ---------------------------------------------------------------------------
// FC side: notify IRQ raised by the cluster.
// ---------------------------------------------------------------------------

static void __pi_cluster_fc_notif_handler(void *arg)
{
    (void)arg;

    // A cluster PE may have a printf line ready: the FC does the semihosting
    // write the cluster core cannot do itself.
    __pi_libc_cluster_flush();

    // The controller reports task completion through the same IRQ.
    if (__pi_cluster_job.done && __pi_cluster_task_event)
    {
        pi_evt_t *event = __pi_cluster_task_event;
        // Surface the completion status to the caller's task (async path).
        if (__pi_cluster_task)
            __pi_cluster_task->status = __pi_cluster_job.status;
        __pi_cluster_task_event = NULL;
        __pi_cluster_task = NULL;
        pi_evt_notify_unsafe(event);
    }

    // A cluster core posted an event to be processed on the FC side
    // (pi_cl_send_task_to_fc). Clear the slot first so the cluster can post
    // again, then signal the controller that the slot is free, and finally
    // notify the event.
    if (__pi_cl_to_fc_event)
    {
        pi_evt_t *cl_event = (pi_evt_t *)__pi_cl_to_fc_event;
        __pi_cl_to_fc_event = NULL;
        cluster_eu_trigger_core_event(CLUSTER_CONTROLLER_CORE,
                                      CLUSTER_EU_CL_TO_FC_SLOT_FREE_EVENT);
        pi_evt_notify_unsafe(cl_event);
    }
}


// ---------------------------------------------------------------------------
// FC side: control API.
// ---------------------------------------------------------------------------

int pi_cluster_scratch_size_set(pi_device_t *dev, unsigned int size)
{
    pi_cluster_t *device = (pi_cluster_t *)dev;

    if (device->open_count != 0)
        return -1;

    device->conf.scratch_size = size;
    return 0;
}


int pi_cluster_open(pi_device_t *dev)
{
    pi_cluster_t *device = (pi_cluster_t *)dev;

    if (device->open_count++ != 0)
        return 0;

    // 1. Initialize the cluster L1 globals (PI_CL_L1_GLOBAL / .l1_data). L1 is
    //    not preloaded, so copy the init image from its L2 load address into L1
    //    on every open, before fetch-enable so the data is in place before any
    //    cluster core boots.
    size_t l1_data_size = __cluster_l1_data_end - __cluster_l1_data_start;
    if (l1_data_size)
        memcpy(__cluster_l1_data_start, __cluster_l1_data_load, l1_data_size);

    // 2. Reserve the cluster scratch region once, just below the controller
    //     boot stack: the worker fork stacks and the
    //     pi_cl_l1_scratch_alloc temporaries live here, so the controller loop
    //     never allocates per task. The band is anchored at CLUSTER_SCRATCH_TOP;
    //     its size comes from the device scratch_size (0 = default), rounded to 16 and
    //     clamped to the TCDM left below the boot stack. Published for the asm
    //     worker/controller loops; the controller only resets stack_size /
    //     scratch_used per task.
    uint32_t scratch_size = device->conf.scratch_size;
    if (scratch_size == 0)
        scratch_size = PI_CL_DEFAULT_SCRATCH_SIZE;
    scratch_size = __pi_round_up(scratch_size, 16);
    if (scratch_size > CLUSTER_L1_SIZE - CLUSTER_BOOT_STACKS_SIZE)
        scratch_size = CLUSTER_L1_SIZE - CLUSTER_BOOT_STACKS_SIZE;

    __pi_cluster_scratch_base = CLUSTER_SCRATCH_TOP - scratch_size;
    __pi_cluster_scratch_size = scratch_size;
    __pi_cluster_stacks_top   = CLUSTER_SCRATCH_TOP;
    __pi_cluster_scratch_used = 0;

    // 3. Hook the cluster -> FC notify IRQ.
    __pi_cluster_task_event = NULL;
    __pi_cluster_fc_notif_addr = SOC_FC_ITC_ADDR + ITC_STATUS_SET_OFFSET;
    pi_irq_handler_set(FC_IRQ_CLUSTER_NOTIF, __pi_cluster_fc_notif_handler, NULL);
    pi_irq_enable(FC_IRQ_CLUSTER_NOTIF);

    // 3b. Enable the hierarchical instruction cache. Its enable register
    //     resets to 0, i.e. every private L0 bypassed and the shared L1.5 off,
    //     so without this the cluster fetches every instruction line from the
    //     L2. No core is fetching yet, so the caches start cold and coherent
    //     with whatever the FC has just written.
    *(volatile uint32_t *)CLUSTER_ICACHE_ENABLE = CLUSTER_ICACHE_ENABLE_ALL;

    // 3c. Set the cluster timer up as the pi_cl_cycle counter: 64-bit
    //     cascaded, stopped and cleared, until pi_cl_cycle_start.
    PI_CL_TIMER_REG(PI_CL_TIMER_CFG_LO) = PI_CL_TIMER_CFG_LO_CASC;
    PI_CL_TIMER_REG(PI_CL_TIMER_RESET_LO) = 1;
    PI_CL_TIMER_REG(PI_CL_TIMER_RESET_HI) = 1;

    // 4. Reset the task slot and boot the cores, if not already done by a
    //    previous open: the controller boots to the event-unit task wait-loop,
    //    the workers to the dispatch-FIFO loop.
    __pi_cluster_job.entry = NULL;
    __pi_cluster_job.arg = NULL;
    __pi_cluster_job.done = 0;
    __asm__ volatile("" ::: "memory");

    if (__pi_cluster_booted)
        return 0;

    __pi_cluster_booted = 1;

    for (int i = 0; i < CLUSTER_NB_CORE; i++)
    {
        void (*boot)(void) = (i == CLUSTER_CONTROLLER_CORE)
            ? __pi_cluster_controller_entry
            : __pi_cluster_worker_entry;
        *(volatile uint32_t *)CLUSTER_CTRL_BOOTADDR(i) = (uint32_t)(uintptr_t)boot;
    }
    *(volatile uint32_t *)CLUSTER_CTRL_FETCH_EN = (1u << CLUSTER_NB_CORE) - 1;

    return 0;
}


void pi_cluster_close(pi_device_t *dev)
{
    pi_cluster_t *device = (pi_cluster_t *)dev;

    if (device->open_count == 0 || --device->open_count != 0)
        return;

    // The cores stay asleep in their wait loops (see the top of this file),
    // only the notifications are turned off.
    pi_irq_disable(FC_IRQ_CLUSTER_NOTIF);
}


int pi_cluster_send_task_to_cl(pi_device_t *device, pi_cluster_task_t *task)
{
    (void)device;

    pi_evt_t evt;
    pi_evt_sig_init(&evt);

    __pi_cluster_job.entry = task->entry;
    __pi_cluster_job.arg = task->arg;
    __pi_cluster_job.slave_stack_size = task->slave_stack_size;
    __pi_cluster_job.status = 0;
    __pi_cluster_job.done = 0;
    __pi_cluster_task = task;
    __pi_cluster_task_event = &evt;
    // Publish the task slot before waking the controller.
    __asm__ volatile("" ::: "memory");

    // Wake the controller core (software event on its event unit).
    cluster_eu_trigger_core_event(CLUSTER_CONTROLLER_CORE, CLUSTER_EU_TASK_EVENT);

    // Sleep until the controller reports completion through the notify IRQ.
    pi_evt_sig_wait(&evt);

    return task->status;
}


void pi_cl_send_task_to_fc(pi_evt_t *event)
{
    // Acquire EU mutex 0 for exclusive access to the shared CL→FC slot.
    cluster_eu_mutex_lock(0);

    // Wait until any previous event has been consumed by the FC handler.
    while (*(volatile pi_evt_t **)&__pi_cl_to_fc_event != NULL)
    {
        cluster_eu_mutex_unlock(0);
        // Sleep until the FC signals that the slot is free again.
        cluster_eu_evt_mask_wait_clear(1u << CLUSTER_EU_CL_TO_FC_SLOT_FREE_EVENT);
        cluster_eu_mutex_lock(0);
    }

    // Publish the event then raise the FC notify IRQ.
    __pi_cl_to_fc_event = event;
    __asm__ volatile("" ::: "memory");
    *(volatile uint32_t *)__pi_cluster_fc_notif_addr = 1u << FC_IRQ_CLUSTER_NOTIF;

    cluster_eu_mutex_unlock(0);
}


int pi_cluster_send_task_to_cl_async(pi_device_t *device,
                                     pi_cluster_task_t *task, pi_evt_t *event)
{
    (void)device;

    __pi_cluster_job.entry = task->entry;
    __pi_cluster_job.arg = task->arg;
    __pi_cluster_job.slave_stack_size = task->slave_stack_size;
    __pi_cluster_job.status = 0;
    __pi_cluster_job.done = 0;
    __pi_cluster_task = task;
    __pi_cluster_task_event = event;
    __asm__ volatile("" ::: "memory");

    // Wake the controller; completion is reported asynchronously through the
    // notify IRQ, which notifies the caller's event.
    cluster_eu_trigger_core_event(CLUSTER_CONTROLLER_CORE, CLUSTER_EU_TASK_EVENT);

    return 0;
}
