// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#if defined(CONFIG_MEMORY_INC)
#include CONFIG_MEMORY_INC
#endif

/**
 * @brief Put a variable into the tiny section.
 *
 * This define puts the associated variable into the main processor tiny section. The tiny
 * section is a special section, close to address 0, which allows the compiler to consider the
 * variable as having a small address, and to apply optimizations.
 */
#ifndef PI_MEMORY_TINY
#define PI_MEMORY_TINY
#endif

#ifndef CONFIG_KERNEL_NB_CORES
#define CONFIG_KERNEL_NB_CORES 1
#endif

/**
 * @brief Declare a per-core variable.
 *
 * On a chip whose fabric controller has several cores running the kernel, each
 * core has its own instance of the variable. It is thread-local storage: each
 * core points the tp register to its own copy of the per-core variables, and
 * the compiler reaches them relative to tp. Kernel threads do not change tp,
 * so all the threads of a core share its instance. With a single core, it is a
 * variable of the tiny section.
 */
#if CONFIG_KERNEL_NB_CORES > 1
#define PI_CORE_LOCAL __thread
#else
#define PI_CORE_LOCAL PI_MEMORY_TINY
#endif
