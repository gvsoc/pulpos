// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <stdint.h>
#include <pmsis/kernel/kernel.h>

/**
 * @addtogroup fc_core_apis
 * @{
 */

// Cores running the kernel. On a chip whose fabric controller has several
// cores, each one runs its own instance of the kernel: its own threads, events
// and interrupt handlers. Threads created on a core only run on that core. Core
// 0 runs main and starts the other ones with pi_fc_core_start.

#ifndef CONFIG_KERNEL_NB_CORES
#define CONFIG_KERNEL_NB_CORES 1
#endif

/**
 * @brief Number of cores running the kernel.
 *
 * @return The number of cores.
 */
static inline int pi_fc_core_nb(void)
{
    return CONFIG_KERNEL_NB_CORES;
}

/**
 * @brief Index of the calling core among the cores running the kernel.
 *
 * @return The core index, 0 for the core running main.
 */
static inline int pi_fc_core_id(void)
{
#if CONFIG_KERNEL_NB_CORES > 1
    int hartid;
    asm ("csrr %0, mhartid" : "=r" (hartid));
    return hartid - CONFIG_KERNEL_CORE0_HARTID;
#else
    return 0;
#endif
}

/**
 * @brief Start a core.
 *
 * The core initializes its own kernel instance, then calls entry with arg on
 * the given stack, in its main thread. The entry return value is the core
 * status, given by pi_fc_core_join. Once entry has returned, the core keeps
 * running the other threads it has created, if any.
 *
 * @param core Index of the core to start, between 1 and pi_fc_core_nb() - 1.
 * @param entry Function executed by the core.
 * @param arg Argument given to the function.
 * @param stack Stack of the core main thread.
 * @param stack_size Size of the stack in bytes.
 *
 * @return 0 on success, -1 if the core does not exist or is already running.
 */
int pi_fc_core_start(int core, int (*entry)(void *arg), void *arg, void *stack,
    unsigned int stack_size);

/**
 * @brief Wait until a core has returned from its entry function.
 *
 * @param core Index of the core, as given to pi_fc_core_start.
 *
 * @return The value returned by the core entry function.
 */
int pi_fc_core_join(int core);

/**
 * @}
 */
