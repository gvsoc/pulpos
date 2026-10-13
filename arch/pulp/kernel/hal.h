// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <kernel/riscv.h>
#include <kernel/semihost.h>

#define PI_LIBC_PUTC_BUFFER_SIZE 128

// Writes the lines the cluster PEs have handed to the FC (see hal.c)
void __pi_libc_cluster_flush(void);

#if defined(CONFIG_IO_STDOUT)

// No semihosting (the RTL testbench): printf goes through the stdout window
// of the SoC, one slot per core (the testbench prints each core's lines), and
// exit through the core status register of the APB SoC control, which the
// testbench polls.

#define PI_STDOUT_ADDR                 0x1A10F000
#define PI_APB_SOC_CORESTATUS_ADDR     0x1A1040A0
#define PI_APB_SOC_CORESTATUS_EOC      (1u << 31)

static inline void __pi_init_platform_exit(int status)
{
    *(volatile uint32_t *)PI_APB_SOC_CORESTATUS_ADDR = PI_APB_SOC_CORESTATUS_EOC | (uint32_t)status;
    while(1);
}

static inline void __pi_libc_write(int fd, uint8_t *buffer, int len)
{
    (void)fd;
    uint32_t hartid;
    __asm__ volatile ("csrr %0, mhartid" : "=r" (hartid));
    volatile uint32_t *stdout_slot = (volatile uint32_t *)(PI_STDOUT_ADDR +
        (((hartid >> 5) & 0x1f) << 7) + ((hartid & 0xf) << 3));
    for (int i = 0; i < len; i++)
        *stdout_slot = buffer[i];
}

#else

static inline void __pi_init_platform_exit(int status)
{
    __pi_libc_semihost_exit(status == 0 ? SEMIHOST_EXIT_SUCCESS : SEMIHOST_EXIT_ERROR);
    while(1);
}

static inline void __pi_libc_write(int fd, uint8_t *buffer, int len)
{
    __pi_libc_semihost_write(fd, buffer, len);
}

#endif



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
