// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <stdio.h>
#include <stdint.h>
#include <kernel/hal.h>

// Line buffers, one per core since the data is shared by the cores of the
// cluster
static char __pi_libc_buffer[CONFIG_SOFTHIER_NB_CORE_PER_CLUSTER][PI_LIBC_PUTC_BUFFER_SIZE];
static int __pi_libc_buffer_index[CONFIG_SOFTHIER_NB_CORE_PER_CLUSTER];

void __pi_init_soc()
{
#ifdef CONFIG_SOFTHIER_MULTICORE
    // The other cores of the cluster wait here before entering main (crt0.S)
    pi_softhier_cluster_barrier();
#endif
}

void __pi_softhier_write(uint8_t *buffer, int len)
{
    volatile uint32_t *putc_reg = (volatile uint32_t *)SOFTHIER_CLUSTER_REG_PUTC;

#ifdef CONFIG_SOFTHIER_MULTICORE
    // The character register assembles one line for the whole cluster: the
    // cores take turns (the lock is reset by core 0 in crt0.S)
    uint32_t *lock = (uint32_t *)pi_softhier_tcdm(SOFTHIER_PUTC_LOCK_OFFSET);
    while (__atomic_exchange_n(lock, 1, __ATOMIC_ACQUIRE));
#endif

    for (int i = 0; i < len; i++)
    {
        *putc_reg = buffer[i];
    }

#ifdef CONFIG_SOFTHIER_MULTICORE
    __atomic_store_n(lock, 0, __ATOMIC_RELEASE);
#endif
}

int __pi_libc_fputc_safe(int c, FILE *stream)
{
    uint32_t core = pi_softhier_core_id();
    char *buffer = __pi_libc_buffer[core];
    int *index = &__pi_libc_buffer_index[core];

    buffer[*index] = c;
    *index = *index + 1;

    if (*index == PI_LIBC_PUTC_BUFFER_SIZE || c == '\n')
    {
        buffer[*index] = 0;

        __pi_libc_write(1, (uint8_t *)buffer, *index);
        *index = 0;
    }

    return 0;
}

// Global barrier. Core 0 of each cluster takes part on behalf of its cluster:
// it increments the arrival counter in the TCDM of cluster 0 and the last one
// to arrive resets it and moves the iteration counter, which the others poll.
// The arrival counter is reset at boot by cluster 0 (crt0.S), the iteration
// counter only needs to change.
void pi_softhier_global_barrier()
{
#ifdef CONFIG_SOFTHIER_MULTICORE
    pi_softhier_cluster_barrier();
#endif

    if (pi_softhier_core_id() == 0)
    {
        uint32_t *counter = (uint32_t *)pi_softhier_remote_tcdm(0,
            SOFTHIER_GLOBAL_BARRIER_OFFSET);
        volatile uint32_t *iter = (volatile uint32_t *)pi_softhier_remote_tcdm(0,
            SOFTHIER_GLOBAL_BARRIER_ITER_OFFSET);

        uint32_t prev_iter = *iter;

        if (__atomic_fetch_add(counter, 1, __ATOMIC_RELAXED) == CONFIG_SOFTHIER_NB_CLUSTER - 1)
        {
            *(volatile uint32_t *)counter = 0;
            __atomic_fetch_add((uint32_t *)iter, 1, __ATOMIC_RELAXED);
        }
        else
        {
            while (*iter == prev_iter);
        }
    }

#ifdef CONFIG_SOFTHIER_MULTICORE
    pi_softhier_cluster_barrier();
#endif
}
