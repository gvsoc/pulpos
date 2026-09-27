// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <stdint.h>
#include <kernel/riscv.h>
#include <arch/softhier/kernel/softhier.h>

#define PI_LIBC_PUTC_BUFFER_SIZE 128

// Every core reports its end of computation, the simulation stops once all of
// them did. The core then sleeps for good.
static inline void __pi_init_platform_exit(int status)
{
    pi_softhier_eoc(status);
    while(1)
    {
        __asm__ volatile("wfi");
    }
}

void __pi_softhier_write(uint8_t *buffer, int len);

// Output goes through the character register of the cluster, which prints a
// line once complete, so that lines of different clusters do not mix.
static inline void __pi_libc_write(int fd, uint8_t *buffer, int len)
{
    __pi_softhier_write(buffer, len);
}


extern unsigned char __pi_irq_vector_base;

static inline uint_t __pi_linker_irq_vector_base()
{
    return (long)&__pi_irq_vector_base;
}


extern unsigned char __pi_fast_irq_vector_base;

static inline uint_t __pi_linker_fast_irq_vector_base()
{
    return (long)&__pi_fast_irq_vector_base;
}
