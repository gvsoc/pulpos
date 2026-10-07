// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

// Each core running the kernel schedules its own threads, executes its own event callbacks and
// has its own interrupt handlers: the kernel variables doing that are per-core variables
// (PI_CORE_LOCAL, see pmsis/kernel/memory.h), with an instance per core reached through tp.

#ifndef CONFIG_KERNEL_NB_CORES
#define CONFIG_KERNEL_NB_CORES 1
#endif

#ifndef LANGUAGE_ASSEMBLY

#include <stdint.h>
#include <pmsis/kernel/memory.h>
#include <pmsis/kernel/irq.h>
#include <pmsis/kernel/spinlock.h>

#if CONFIG_KERNEL_NB_CORES > 1

// Per-core variable blocks: the template (initialized part, then zero part) and one block per
// core, placed by the linker script
extern char __pi_tls_start[];
extern char __pi_tls_blocks[];
// Sizes, given by the linker as symbol addresses
extern char __pi_tls_size[];
extern char __pi_tls_data_size[];

// Block of the per-core variables of a core
static inline __attribute__((always_inline)) char *__pi_core_local_block(int core)
{
    return __pi_tls_blocks + core * (uintptr_t)__pi_tls_size;
}

// Block of the per-core variables of the calling core
static inline __attribute__((always_inline)) char *__pi_core_local_self()
{
    char *block;
    asm ("mv %0, tp" : "=r" (block));
    return block;
}

// Address of the instance of a per-core variable of another core: at the same offset in its
// block as the instance of the calling core in its own
#define __PI_CORE_LOCAL_OF(var, core) \
    ((__typeof__(&(var)))(__pi_core_local_block(core) + ((char *)&(var) - __pi_core_local_self())))

// Protects the kernel data shared by the cores, like the memory allocators
extern pi_spinlock_t __pi_kernel_lock;
#endif

// Disable interrupts and take the kernel lock, to access kernel data shared by the cores. Only
// the cores running the kernel can take it, the cluster cores cannot take spinlocks.
static inline __attribute__((always_inline)) int __pi_kernel_lock_irq()
{
    int irq = pi_irq_lock();
#if CONFIG_KERNEL_NB_CORES > 1
    pi_spinlock_take(&__pi_kernel_lock);
#endif
    return irq;
}

static inline __attribute__((always_inline)) void __pi_kernel_unlock_irq(int irq)
{
#if CONFIG_KERNEL_NB_CORES > 1
    pi_spinlock_release(&__pi_kernel_lock);
#endif
    pi_irq_unlock(irq);
}

#endif

// Assembly access to a per-core variable. With a single core, it is a variable of the tiny
// section, reached in one access.
#if CONFIG_KERNEL_NB_CORES > 1
#define PI_CORE_LOCAL_ACCESS(var)       %tprel_lo(var)(tp)
#else
#define PI_CORE_LOCAL_ACCESS(var)       %tiny(var)(x0)
#endif
