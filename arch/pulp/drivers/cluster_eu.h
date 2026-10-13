// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Minimal cluster event-unit access helpers (eu_v3) used by the cluster
// driver: per-core event mask, hardware barrier 0 and the dispatch FIFO. All
// accesses go through the calling core's demux window so the blocking reads
// (barrier trigger-wait, dispatch pop) put the core to sleep.

#pragma once

// The register-address and event-number #defines below are also consumed by
// cluster_entry.S, so keep the C-only parts (stdint, inline helpers) out of the
// assembler's way.
#ifndef __ASSEMBLER__

#include <stdint.h>

// Blocking event-unit reads use the p.elw event-load instruction: the core
// is clock-gated until the event unit answers, and the access is replayed
// after an interrupt handler if an IRQ wakes the core. The toolchain
// exposes it through __builtin_pulp_event_unit_read.
static inline uint32_t cluster_eu_read_wait(uint32_t addr)
{
    return __builtin_pulp_event_unit_read((int *)addr, 0);
}

#endif // __ASSEMBLER__

// Controller (master) PE index: the core the FC targets for EU events (task
// wake, slot-free). Defined here (rather than only in cluster_implem.h) so
// that any file that includes cluster_eu.h can call
// cluster_eu_trigger_core_event without pulling in the full private header.
#define CLUSTER_CONTROLLER_CORE      0

// Per-core demux window (each PE sees its own core port here).
#define CLUSTER_EU_DEMUX_BASE        0x10204000u

// Demux areas (archi/eu_v3.h).
#define CLUSTER_EU_CORE_DEMUX        (CLUSTER_EU_DEMUX_BASE + 0x000u)
#define CLUSTER_EU_DISPATCH_DEMUX    (CLUSTER_EU_DEMUX_BASE + 0x080u)
#define CLUSTER_EU_MUTEX_DEMUX       (CLUSTER_EU_DEMUX_BASE + 0x0C0u)
#define CLUSTER_EU_BARRIER_DEMUX     (CLUSTER_EU_DEMUX_BASE + 0x200u)

// Core area registers.
#define CLUSTER_EU_CORE_MASK_AND     (CLUSTER_EU_CORE_DEMUX + 0x04u)
#define CLUSTER_EU_CORE_MASK_OR      (CLUSTER_EU_CORE_DEMUX + 0x08u)
#define CLUSTER_EU_CORE_EVENT_WAIT_CLEAR (CLUSTER_EU_CORE_DEMUX + 0x3Cu)

// Dispatch registers.
#define CLUSTER_EU_DISPATCH_FIFO     (CLUSTER_EU_DISPATCH_DEMUX + 0x0u)
#define CLUSTER_EU_DISPATCH_TEAM     (CLUSTER_EU_DISPATCH_DEMUX + 0x4u)

// Barrier 0 registers. Barrier 0 is the team barrier: it joins the cores of
// the current fork, the master included, for pi_cl_team_barrier and for the
// closing rendez-vous of the fork.
#define CLUSTER_EU_BARR_TRIGGER_MASK      (CLUSTER_EU_BARRIER_DEMUX + 0x00u)
#define CLUSTER_EU_BARR_TARGET_MASK       (CLUSTER_EU_BARRIER_DEMUX + 0x0Cu)
#define CLUSTER_EU_BARR_TRIGGER_WAIT_CLEAR (CLUSTER_EU_BARRIER_DEMUX + 0x1Cu)

// Event numbers routed to the cores.
#define CLUSTER_EU_DMA0_EVENT        8
#define CLUSTER_EU_DMA1_EVENT        9
#define CLUSTER_EU_BARRIER_EVENT     16
#define CLUSTER_EU_MUTEX_EVENT       17
#define CLUSTER_EU_DISPATCH_EVENT    18

// Software event the FC raises on the controller core to hand it a task. A
// free SW event slot (0..7 are software; the cluster uses 8/9 for DMA, 12 for
// accelerators, 16/18 for barrier/dispatch; tests use 0).
#define CLUSTER_EU_TASK_EVENT        5

// Software event the FC triggers on the controller core once it has consumed a
// CL→FC event (pi_cl_send_task_to_fc), signalling that the shared slot is
// free for the next post.
#define CLUSTER_EU_CL_TO_FC_SLOT_FREE_EVENT  6

// Global event-unit register window (peripherals/event_unit in the GVSoC
// cluster description). Unlike the per-core demux at 0x1020_4000, this window is
// reachable from the FC through the cluster peripheral interconnect, so the FC
// uses it to trigger an event on a specific cluster core. The software-event
// trigger area sits at EU_SW_EVENTS_AREA_BASE (eu_v3.h); writing the core mask
// to slot `event` raises `event` on those cores.
#define CLUSTER_EU_GLOBAL_BASE       0x10200800u
#define CLUSTER_EU_SW_EVENTS_AREA    0x600u

#ifndef __ASSEMBLER__

// Cluster side: acquire hardware mutex `id` (blocking read via p.elw; the
// core sleeps until the mutex is free and ownership is transferred to it).
// The hardware completes every mutex read through the core's event unit, a
// free mutex included: the core clock stops on the access and restarts on
// the mutex event, so CLUSTER_EU_MUTEX_EVENT must be in the core's event mask
// or the read never returns.
static inline void cluster_eu_mutex_lock(unsigned int id)
{
    (void)cluster_eu_read_wait(CLUSTER_EU_MUTEX_DEMUX + id * 4u);
}

// Cluster side: release hardware mutex `id` (write any value to unlock;
// the EU transfers ownership to the first waiting core, if any).
static inline void cluster_eu_mutex_unlock(unsigned int id)
{
    *(volatile uint32_t *)(CLUSTER_EU_MUTEX_DEMUX + id * 4u) = 0;
}

// FC side: raise `event` on a single cluster `core` via the global EU window.
static inline void cluster_eu_trigger_core_event(int core, int event)
{
    *(volatile uint32_t *)(CLUSTER_EU_GLOBAL_BASE + CLUSTER_EU_SW_EVENTS_AREA
                           + (uint32_t)event * 4) = 1u << core;
}

static inline void cluster_eu_evt_mask_or(uint32_t mask)
{
    *(volatile uint32_t *)CLUSTER_EU_CORE_MASK_OR = mask;
}

// Blocking: OR the events into this core's mask, sleep until one of the
// masked events fires (the buffered events are returned and cleared), then
// remove the events from the mask again.
static inline uint32_t cluster_eu_evt_mask_wait_clear(uint32_t mask)
{
    *(volatile uint32_t *)CLUSTER_EU_CORE_MASK_OR = mask;
    uint32_t events = cluster_eu_read_wait(CLUSTER_EU_CORE_EVENT_WAIT_CLEAR);
    *(volatile uint32_t *)CLUSTER_EU_CORE_MASK_AND = mask;
    return events;
}

// Blocking: sleep until a masked event fires; returns and clears the buffered
// events without touching the mask (set once with cluster_eu_evt_mask_or).
static inline uint32_t cluster_eu_evt_wait_clear(void)
{
    return cluster_eu_read_wait(CLUSTER_EU_CORE_EVENT_WAIT_CLEAR);
}

static inline void cluster_eu_dispatch_team_config(uint32_t core_mask)
{
    *(volatile uint32_t *)CLUSTER_EU_DISPATCH_TEAM = core_mask;
}

static inline void cluster_eu_dispatch_push(uint32_t value)
{
    *(volatile uint32_t *)CLUSTER_EU_DISPATCH_FIFO = value;
}

// Blocking: sleeps until a value this core is elected for is pushed.
static inline uint32_t cluster_eu_dispatch_pop(void)
{
    return cluster_eu_read_wait(CLUSTER_EU_DISPATCH_FIFO);
}

static inline void cluster_eu_barrier_setup(uint32_t core_mask)
{
    *(volatile uint32_t *)CLUSTER_EU_BARR_TRIGGER_MASK = core_mask;
    *(volatile uint32_t *)CLUSTER_EU_BARR_TARGET_MASK = core_mask;
}

// Blocking: triggers this core's bit and sleeps until the barrier releases.
static inline void cluster_eu_barrier_trigger_wait_clear(void)
{
    (void)cluster_eu_read_wait(CLUSTER_EU_BARR_TRIGGER_WAIT_CLEAR);
}

#endif // __ASSEMBLER__
