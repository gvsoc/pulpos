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

// This file can be included to have access to chip-dependent code implementing the HAL

#pragma once

#include <stdint.h>

// Write a buffer to a file descriptor
static inline void __pi_libc_write(int fd, uint8_t *buffer, int len);

// Make the platform exit with the specified status
static inline void __attribute__((noreturn)) __pi_init_platform_exit(int status);

// Initialize all the soc
void __pi_init_soc();

// Now include the chip-specific header
#include <pmsis/kernel/kernel.h>
#include PI_CHIP_INC(CONFIG_CHIP_FAMILY_NAME, kernel/hal.h)
