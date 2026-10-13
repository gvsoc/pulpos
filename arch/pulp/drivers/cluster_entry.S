// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// PULP cluster PE boot trampolines. The controller (core 0) keeps a full 2 KB
// boot stack at the top of TCDM and runs the asm task wait-loop. The workers
// (cores 1..7) run an idle loop kept entirely in registers with no boot stack at
// all: they take no interrupts while idle (they wake from p.elw events, not
// traps), so a stack is only ever needed for the fork entry, which they carve
// from the task's L1 scratch just before jumping. The cluster shares the FC
// binary's L2 .data/.bss, already initialised by the FC crt0; neither loop
// returns.

#include "cluster_implem.h"
#include "cluster_eu.h"

    .section .text

    // Controller (core 0): full 2 KB boot stack at the top of TCDM, then the
    // task wait-loop. The loop invariants stay in callee-saved registers (the
    // task entry preserves them per the ABI), and the task entry is dispatched
    // frameless: ra is preset to the loop tail so the entry's own `ret` lands
    // back in the loop. The scratch is reserved once at pi_cluster_open, so the
    // loop allocates nothing: it just sets this task's worker stack size and
    // raises the FC completion IRQ itself.
    //   s0 = EU core event wait-clear reg   s1 = &__pi_cluster_job
    //   s2 = FC notify register
    .global __pi_cluster_controller_entry
__pi_cluster_controller_entry:
    li    sp, CLUSTER_L1_END                       // 2 KB controller boot stack

#ifdef CONFIG_STACK_CHECK
    // Declare the controller boot stack to the simulator stack checker
    li    a0, 0x11A                                // SEMIHOSTING_GV_STACK_SET
    li    a1, CLUSTER_L1_END - CLUSTER_CC_STACK_SIZE
    li    a2, CLUSTER_CC_STACK_SIZE
    .option norvc
    slli  zero, zero, 0x1f
    ebreak
    srai  zero, zero, 0x7
    .option rvc
#endif

    li    s0, CLUSTER_EU_CORE_EVENT_WAIT_CLEAR
    la    s1, __pi_cluster_job
    la    s2, __pi_cluster_fc_notif_addr           // set before the boot, fixed until close
    lw    s2, 0(s2)

    // Sleep on the team barrier (the controller is the fork master) and the
    // task software event the FC raises to hand over work. The mutex event
    // is needed too: a mutex lock is a sleeping access which only completes
    // on that event (pi_cl_send_task_to_fc locks mutex 0 from the task).
    li    t0, (1 << CLUSTER_EU_BARRIER_EVENT) | (1 << CLUSTER_EU_TASK_EVENT) | (1 << CLUSTER_EU_MUTEX_EVENT)
    li    t1, CLUSTER_EU_CORE_MASK_OR
    sw    t0, 0(t1)

__pi_cluster_controller_wait_loop:
    p.elw t0, 0(s0)                                // sleep until the FC posts
    andi  t1, t0, (1 << CLUSTER_EU_TASK_EVENT)     // only the task event resumes
    beqz  t1, __pi_cluster_controller_wait_loop

    // Set this task's worker stack size. The scratch region was reserved at
    // open; the worker stacks are carved NB_WORKERS * stack_size from its top,
    // so fail the task (status -1) if they would not fit.
    lw    a4, PI_CLUSTER_JOB_ENTRY(s1)             // task entry
    lw    t0, PI_CLUSTER_JOB_SLAVE_STACK_SIZE(s1)
    addi  t0, t0, 15
    andi  t0, t0, -16                              // stack_size = round_up(.,16)
    li    t1, CLUSTER_NB_WORKERS
    mul   t2, t0, t1                               // NB_WORKERS * stack_size
    la    t3, __pi_cluster_scratch_size            // reserved size (set per open)
    lw    t3, 0(t3)
    bgtu  t2, t3, 9f                               // worker stacks exceed scratch

    la    t4, __pi_cluster_stack_size
    sw    t0, 0(t4)                                // publish worker stack size
    la    t4, __pi_cluster_scratch_used
    sw    x0, 0(t4)                                // reset scratch_alloc cursor
    sw    x0, PI_CLUSTER_JOB_STATUS(s1)            // status 0

    beqz  a4, __pi_cluster_controller_finish       // no entry -> just report done
    lw    a0, PI_CLUSTER_JOB_ARG(s1)               // arg
    la    ra, __pi_cluster_controller_finish       // entry's `ret` returns here
    jr    a4                                        // entry(arg)

9:
    li    t0, -1
    sw    t0, PI_CLUSTER_JOB_STATUS(s1)            // worker stacks do not fit

__pi_cluster_controller_finish:
    // Report completion to the FC: done flag, then raise its software IRQ.
    li    t0, 1
    sw    t0, PI_CLUSTER_JOB_DONE(s1)
    li    t0, (1 << FC_IRQ_CLUSTER_NOTIF)
    sw    t0, 0(s2)
    j     __pi_cluster_controller_wait_loop

    // Worker idle loop. The worker is the root of its call chain and runs with
    // no stack, so it owns every register at no cost: the loop invariants stay
    // in callee-saved registers (set once, preserved across the fork entry by
    // the ABI), and ra is preset once to the loop head so the fork entry's own
    // `ret` does the loop-back, with the closing barrier at the loop head. The
    // p.elw pops/barrier write only their rd, so they clobber none of these.
    //   s1 = entry (per iteration)   s2 = core_id - 1
    //   s3 = barrier reg   s4 = dispatch FIFO
    //   s5 = &stack_size   s6 = &stacks_top
    .global __pi_cluster_worker_entry
__pi_cluster_worker_entry:
    csrr  s2, mhartid
    andi  s2, s2, 0x1f
    addi  s2, s2, -1                              // worker index (invariant)
    li    s3, CLUSTER_EU_BARR_TRIGGER_WAIT_CLEAR  // closing rendez-vous with the master
    li    s4, CLUSTER_EU_DISPATCH_FIFO
    la    s5, __pi_cluster_stack_size
    la    s6, __pi_cluster_stacks_top

    // Enable the dispatch + barrier events in this core's event mask.
    li    t0, CLUSTER_EU_CORE_MASK_OR
    li    t1, (1 << CLUSTER_EU_BARRIER_EVENT) | (1 << CLUSTER_EU_DISPATCH_EVENT)
    sw    t1, 0(t0)

    la    ra, 2f                      // ra = loop head, preset once
    j     3f                          // first iteration: no fork to close yet

2:                                    // loop head: a fork entry just returned here
    p.elw t2, 0(s3)                   // close the just-finished fork (barrier)
3:
    p.elw s1, 0(s4)                   // entry  (used far below, at jr)
    p.elw a0, 0(s4)                   // arg, straight into the entry's a0

    // Per-core fork stack carved from the task scratch (the only stack a worker
    // ever uses): sp = stacks_top - worker_index * stack_size. Both operands are
    // loaded up front so the load-use penalty is hidden.
    lw    t1, 0(s5)                   // stack_size (per fork)
    lw    t2, 0(s6)                   // stacks_top (per task) - loaded early
    mul   t0, s2, t1                  // worker_index * stack_size

#ifdef CONFIG_STACK_CHECK
    // Declare this fork's stack to the simulator stack checker BEFORE writing
    // sp: the previous fork's bounds are still armed, and the new stack may
    // lie outside them when the task stack size changed. The semihosting
    // sequence uses a0-a2, so the fork arg is parked in t3 around it.
    mv    t3, a0
    sub   a1, t2, t0                  // stack top
    sub   a1, a1, t1                  // base = stack top - stack_size
    mv    a2, t1                      // size
    li    a0, 0x11A                   // SEMIHOSTING_GV_STACK_SET
    .option norvc
    slli  zero, zero, 0x1f
    ebreak
    srai  zero, zero, 0x7
    .option rvc
    mv    a0, t3
#endif

    sub   sp, t2, t0

    jr    s1                          // entry(a0); its ret jumps to 2b (ra preset)
