// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <stdio.h>
#include <stdint.h>
#include <kernel/hal.h>
#include <arch/pulp/kernel/irq.h>
#include <arch/pulp/drivers/cluster_implem.h>

static char __pi_libc_buffer[PI_LIBC_PUTC_BUFFER_SIZE];
static int __pi_libc_buffer_index = 0;

// Per-cluster-core line buffers. A cluster PE cannot do semihosting itself:
// the simulator's semihosting handler reads target memory with a synchronous
// debug request, which the cluster paths do not give. So a cluster PE buffers a
// line here (these statics live in L2) and hands it to the FC, which does the
// actual semihosting write (__pi_libc_cluster_flush). The PE blocks until the FC
// has consumed its buffer, so the slot can be reused.
static char __pi_libc_cluster_buffer[CLUSTER_NB_CORE][PI_LIBC_PUTC_BUFFER_SIZE + 1];
static int __pi_libc_cluster_buffer_index[CLUSTER_NB_CORE] = {0};
static volatile int __pi_libc_cluster_ready[CLUSTER_NB_CORE] = {0};
static int __pi_libc_cluster_len[CLUSTER_NB_CORE] = {0};

// Called by the FC from the cluster notify IRQ handler: flush the cluster line
// buffers that PEs have marked ready.
void __pi_libc_cluster_flush(void)
{
    for (int i = 0; i < CLUSTER_NB_CORE; i++)
    {
        if (__pi_libc_cluster_ready[i])
        {
            __pi_libc_write(1, (uint8_t *)__pi_libc_cluster_buffer[i],
                __pi_libc_cluster_len[i]);
            __asm__ volatile("" ::: "memory");
            __pi_libc_cluster_ready[i] = 0;
        }
    }
}

void __pi_init_soc()
{
    // On the RTL, the boot ROM leaves interrupts unmasked in the ITC (the
    // reference clock one, while it waits for the JTAG load): mask them before
    // the first wait enables the interrupts of the core.
    __pi_irq_init();
}

int __pi_libc_fputc_safe(int c, FILE *stream)
{
#if defined(CONFIG_IO_STDOUT)
    // The stdout window keeps a line per core
    uint8_t byte = c;
    __pi_libc_write(1, &byte, 1);
    return 0;
#else
    // Cluster PEs run with their stack in the cluster L1, the FC in L2
    uintptr_t sp;
    __asm__ volatile ("mv %0, sp" : "=r" (sp));

    if (sp >= CLUSTER_L1_BASE && sp < CLUSTER_L1_END)
    {
        int core_id;
        __asm__ volatile ("csrr %0, mhartid" : "=r" (core_id));
        core_id &= 0x1f;
        char *buffer = __pi_libc_cluster_buffer[core_id];
        int *index = &__pi_libc_cluster_buffer_index[core_id];

        buffer[*index] = c;
        *index = *index + 1;

        if (*index == PI_LIBC_PUTC_BUFFER_SIZE || c == '\n')
        {
            buffer[*index] = 0;
            // Hand the line to the FC: mark the slot ready and wait until the
            // FC has flushed it. The notify IRQ is raised again while waiting:
            // the FC clears the shared ITC bit on entry, so under a concurrent
            // printf storm a single edge can be lost.
            volatile uint32_t *notif = (volatile uint32_t *)__pi_cluster_fc_notif_addr;
            __pi_libc_cluster_len[core_id] = *index;
            __asm__ volatile("" ::: "memory");
            __pi_libc_cluster_ready[core_id] = 1;
            while (__pi_libc_cluster_ready[core_id])
                *notif = 1u << FC_IRQ_CLUSTER_NOTIF;
            *index = 0;
        }

        return 0;
    }

    char *buffer = __pi_libc_buffer;
    int *index = &__pi_libc_buffer_index;

    buffer[*index] = c;
    *index = *index + 1;

    if (*index == PI_LIBC_PUTC_BUFFER_SIZE || c == '\n')
    {
        buffer[*index] = 0;

        __pi_libc_write(1, (uint8_t *)buffer, *index);
        *index = 0;
    }

    return 0;
#endif
}
