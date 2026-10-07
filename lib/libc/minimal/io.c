// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <kernel/init.h>
#include <kernel/core_data.h>
#include "io.h"






void __pi_libc_init()
{
}



int __pi_libc_start()
{
    return 0;
}



void __pi_libc_stop()
{
}






int puts(const char *s)
{
    char c;
    do
    {
        c = *s;
        if (c == 0)
        {
            __pi_libc_fputc_safe('\n', NULL);
            break;
        }
        __pi_libc_fputc_safe(c, NULL);
        s++;
    } while(1);

    return 0;
}



int fputc(int c, FILE *stream)
{
    __pi_libc_fputc_safe(c, NULL);

    return 0;
}



int putchar(int c)
{
    return fputc(c, stdout);
}



int __pi_libc_prf(int (*func)(int,FILE *), void *dest, const char *format, va_list vargs)
{
#if CONFIG_KERNEL_NB_CORES > 1
    // The kernel lock keeps the lines printed by the cores running the kernel apart. The cluster
    // cores, which also print through here, cannot take it, they print through buffers of their
    // own the FC flushes.
    unsigned int hartid;
    asm ("csrr %0, mhartid" : "=r" (hartid));
    if (hartid - CONFIG_KERNEL_CORE0_HARTID >= CONFIG_KERNEL_NB_CORES)
    {
        return __pi_libc_prf_safe(func, dest, format, vargs);
    }

    int irq = __pi_kernel_lock_irq();
    int result = __pi_libc_prf_safe(func, dest, format, vargs);
    __pi_kernel_unlock_irq(irq);
    return result;
#else
    return __pi_libc_prf_safe(func, dest, format, vargs);
#endif
}



void exit(int status)
{
    // Stop the OS
    __pi_init_stop(status);
}



void abort()
{
    exit(-1);
}
