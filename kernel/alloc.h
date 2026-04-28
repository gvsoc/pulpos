// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <pmsis/kernel/irq.h>


typedef struct pi_alloc_block_s
{
    int                      size;
    struct pi_alloc_block_s *next;
} pi_alloc_chunk_t;


typedef struct
{
    pi_alloc_chunk_t *first_free;
} pi_alloc_t;


extern pi_alloc_t __pi_mem_alloc_instances[];


void __pi_alloc_init(void);
void __pi_mem_alloc_init(pi_alloc_t *a, void *_chunk, size_t size);
void *__pi_mem_alloc_align(pi_alloc_t *a, size_t size, size_t align);
void __pi_mem_free(pi_alloc_t *a, void *_chunk, size_t size);
void *__pi_mem_alloc(pi_alloc_t *a, size_t size);


static inline void *pi_mem_alloc(enum pi_mem_allocator allocator, size_t size)
{
    int irq = pi_irq_lock();
    void *chunk = __pi_mem_alloc(&__pi_mem_alloc_instances[allocator], size);
    pi_irq_unlock(irq);
    return chunk;
}


static inline void pi_mem_free(enum pi_mem_allocator allocator, void *data, size_t size)
{
    int irq = pi_irq_lock();
    __pi_mem_free(&__pi_mem_alloc_instances[allocator], data, size);
    pi_irq_unlock(irq);
}


static inline void *pi_mem_alloc_align(enum pi_mem_allocator allocator, size_t size, size_t align)
{
    int irq = pi_irq_lock();
    void *chunk = __pi_mem_alloc_align(&__pi_mem_alloc_instances[allocator], size, align);
    pi_irq_unlock(irq);
    return chunk;
}


static inline void *pi_malloc(size_t size)
{
    return pi_mem_alloc(PI_MEM_ALLOCATOR_DEFAULT, size);
}


static inline void pi_free(void *chunk, size_t size)
{
    pi_mem_free(PI_MEM_ALLOCATOR_DEFAULT, chunk, size);
}
