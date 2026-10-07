// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <pmsis/kernel/kernel.h>

    .section .text
    .global __pi_fc_core_start_stub
    .type   __pi_fc_core_start_stub, @function
    // First code executed by a core started with pi_fc_core_start: take the stack it was given
    // and continue in C with its index.
__pi_fc_core_start_stub:
    csrr  a0, mhartid
    addi  a0, a0, -CONFIG_KERNEL_CORE0_HARTID
    la    t0, __pi_fc_core_sp
    slli  t1, a0, 2
    add   t0, t0, t1
    lw    sp, 0(t0)
    la    t0, __pi_fc_core_init
    jr    t0
    .size   __pi_fc_core_start_stub, . - __pi_fc_core_start_stub
