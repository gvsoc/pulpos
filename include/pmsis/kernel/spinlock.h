// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <stdint.h>
#include <pmsis/kernel/kernel.h>
#include <pmsis/kernel/irq.h>

/**
 * @addtogroup spinlock_apis
 * @{
 */

// Spinlocks protect data shared by the cores running the kernel (see
// pmsis/kernel/fc_core.h). A core takes a lock with a test-and-set access,
// through the chip alias of the memory holding the lock, which returns the word
// and sets it to all ones in one access. The lock must then be in that memory:
// on el1 the FC TCDM, where the data and BSS sections are, and not in the tiny
// section (PI_MEMORY_TINY), which is reached through another alias. Interrupts must be
// disabled while holding a lock, so that an interrupt handler of the same core
// does not try to take it again, which pi_spinlock_lock_irq does.
// On chips with a single core running the kernel, spinlocks do nothing.

#ifndef CONFIG_KERNEL_NB_CORES
#define CONFIG_KERNEL_NB_CORES 1
#endif

typedef struct
{
    volatile uint32_t value;
} pi_spinlock_t;

/**
 * @brief Initialize a spinlock, released.
 *
 * @param lock The spinlock.
 */
static inline void pi_spinlock_init(pi_spinlock_t *lock)
{
    lock->value = 0;
}

/**
 * @brief Take a spinlock, waiting until it is released. Interrupts must be
 * disabled.
 *
 * @param lock The spinlock.
 */
static inline void pi_spinlock_take(pi_spinlock_t *lock)
{
#if CONFIG_KERNEL_NB_CORES > 1
    volatile uint32_t *ts = (volatile uint32_t *)((uintptr_t)&lock->value +
        CONFIG_KERNEL_SPINLOCK_TS_OFFSET);
    while (*ts != 0)
    {
    }
    // Accesses protected by the lock must not be moved above this point
    __asm__ __volatile__ ("" : : : "memory");
#else
    (void)lock;
#endif
}

/**
 * @brief Release a spinlock taken with pi_spinlock_take.
 *
 * @param lock The spinlock.
 */
static inline void pi_spinlock_release(pi_spinlock_t *lock)
{
#if CONFIG_KERNEL_NB_CORES > 1
    // Accesses protected by the lock must be done before it is released
    __asm__ __volatile__ ("" : : : "memory");
    lock->value = 0;
#else
    (void)lock;
#endif
}

/**
 * @brief Disable interrupts and take a spinlock.
 *
 * @param lock The spinlock.
 *
 * @return The interrupt state, to be given to pi_spinlock_unlock_irq.
 */
static inline int pi_spinlock_lock_irq(pi_spinlock_t *lock)
{
    int irq = pi_irq_lock();
    pi_spinlock_take(lock);
    return irq;
}

/**
 * @brief Release a spinlock and restore interrupts.
 *
 * @param lock The spinlock.
 * @param irq The interrupt state returned by pi_spinlock_lock_irq.
 */
static inline void pi_spinlock_unlock_irq(pi_spinlock_t *lock, int irq)
{
    pi_spinlock_release(lock);
    pi_irq_unlock(irq);
}

/**
 * @}
 */
