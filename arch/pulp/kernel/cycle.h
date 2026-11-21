// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <stdint.h>
#include <pmsis/kernel/kernel.h>

// Cycle counter on the RI5CY performance counters of the calling core (FC or
// cluster PE): PCER (0xCC0) selects the counted events, PCMR (0xCC1) starts
// them (bit 0) with saturation (bit 1), and PCCR0 (0x780) counts the cycles.
// Each core has its own counters, a measurement is local to the core doing it.

#define __PI_CSR_PCER  0xCC0
#define __PI_CSR_PCMR  0xCC1
#define __PI_CSR_PCCR0 0x780

#define __PI_PCER_CYCLES      (1 << 0)
#define __PI_PCMR_ACTIVE      (1 << 0)
#define __PI_PCMR_SATURATE    (1 << 1)

ALWAYS_INLINE void pi_cycle_start() {
    uint32_t pcer;
    __asm__ volatile("csrr %0, 0xCC0" : "=r"(pcer));
    __asm__ volatile("csrw 0xCC0, %0" : : "r"(pcer | __PI_PCER_CYCLES));
    __asm__ volatile("csrw 0xCC1, %0" : : "r"(__PI_PCMR_ACTIVE | __PI_PCMR_SATURATE));
}

ALWAYS_INLINE void pi_cycle_stop() {
    __asm__ volatile("csrw 0xCC1, %0" : : "r"(0));
}

ALWAYS_INLINE void pi_cycle_reset() {
    __asm__ volatile("csrw 0x780, %0" : : "r"(0));
}

ALWAYS_INLINE uint32_t pi_cycle_get32() {
    uint32_t val;
    __asm__ volatile("csrr %0, 0x780" : "=r"(val));
    return val;
}

ALWAYS_INLINE uint64_t pi_cycle_get64() {
    return (uint64_t)pi_cycle_get32();
}
