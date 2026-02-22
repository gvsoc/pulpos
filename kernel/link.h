// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

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
