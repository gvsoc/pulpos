// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// SoftHier startup. Every core of every cluster starts here, at the start of
// the instruction memory of its cluster (the linker script places this
// section first, since the cores boot there when the binary is only known
// after the platform is built). Core 0 of each cluster runs the runtime and
// main, the other cores either sleep or, with the multicore option, run main
// too. Every core reports its end of computation to the SoC, which stops the
// simulation once all of them did.

#define SOC_REG_EOC_ALL (CONFIG_SOFTHIER_SOC_REG_BASE + 0x4)

    .section .text.start, "ax"
    .global _start
    .type   _start, @function
_start:
    csrr  t0, mhartid

    // Core 0 of each cluster resets the lock of the character output (hal.c)
    // before the other cores can print. The TCDM is not initialized to zero.
    bnez  t0, 1f
    li    t1, CONFIG_SOFTHIER_TCDM_BASE + CONFIG_SOFTHIER_TCDM_SIZE - 24
    sw    zero, 0(t1)

    // Core 0 of cluster 0 also resets the arrival counter of the global
    // barrier (hal.c): no other cluster can reach its first barrier this
    // early.
    li    t1, CONFIG_SOFTHIER_CLUSTER_REG_BASE
    lw    t1, 0(t1)
    bnez  t1, 1f
    li    t1, CONFIG_SOFTHIER_TCDM_BASE + CONFIG_SOFTHIER_TCDM_SIZE - 8
    sw    zero, 0(t1)
1:

    // Per-core stack, one slot per core below `stack`
    la    x2, stack
    slli  t2, t0, CONFIG_STACK_SIZE_LOG2
    sub   x2, x2, t2

#ifdef CONFIG_SOFTHIER_MULTICORE
    bnez  t0, __pi_hart_secondary
#else
    bnez  t0, __pi_hart_park
#endif

    // Core 0: runtime init, main, then the exit which reports the status
    la    t0, __pi_init_start
    addi  a0, x0, 0
    jalr  x1, t0

#ifdef CONFIG_SOFTHIER_MULTICORE
__pi_hart_secondary:
    // Wait for core 0 to be done with the runtime init (__pi_init_soc)
    csrr  x0, 0x7C2

    la    t0, main
    jalr  x1, t0

    // Report the status returned by main
    li    t0, SOC_REG_EOC_ALL
    sw    a0, 0(t0)
    j     __pi_hart_sleep
#endif

__pi_hart_park:
    li    t0, SOC_REG_EOC_ALL
    sw    zero, 0(t0)

__pi_hart_sleep:
    wfi
    j     __pi_hart_sleep

    .size   _start, . - _start
