/*
 * Copyright (C) 2025 GreenWaves Technologies, ETH Zurich and University of Bologna
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

#pragma once

#include <kernel/riscv.h>
#include <kernel/semihost.h>

#define PI_LIBC_PUTC_BUFFER_SIZE 128

static inline void __pi_init_platform_exit(int status)
{
    __pi_libc_semihost_exit(status == 0 ? SEMIHOST_EXIT_SUCCESS : SEMIHOST_EXIT_ERROR);
    while(1);
}

static inline void __pi_libc_write(int fd, uint8_t *buffer, int len)
{
    __pi_libc_semihost_write(fd, buffer, len);
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
