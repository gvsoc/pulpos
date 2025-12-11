/*
 * Copyright (C) 2023 GreenWaves Technologies, ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <kernel/init.h>
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
    return __pi_libc_prf_safe(func, dest, format, vargs);
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
