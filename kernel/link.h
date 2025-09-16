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

/*
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

// This file can be included to have access to linker script symbols

#include <stdint.h>

// This symbol should point to first byte of BSS in the linker script
extern unsigned int _bss_start;

// This symbol should point to first byte of section next to BSS in the linker script
extern unsigned int _bss_end;

// Return BSS section start address
static inline void *__pi_init_bss_start()
{
    return (void *)&_bss_start;
}

// Return BSS section end address, which is the first address after the last one to be wrritten to 0
static inline uintptr_t __pi_init_bss_end()
{
    return (uintptr_t)&_bss_end;
}
