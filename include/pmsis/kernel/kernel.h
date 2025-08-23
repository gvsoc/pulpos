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

#pragma once


#define ALWAYS_INLINE static inline __attribute__((always_inline))

// This macro can be used to include a chip-dependent file for the current chip
#define  PI_CHIP_INC(chip_name,file) __PI_OS_CHIP_INC(chips/chip_name/file)
#define  __PI_OS_CHIP_INC(x) #x

#ifndef likely
#define likely(x) __builtin_expect(x, 1)
#endif

#ifndef unlikely
#define unlikely(x) __builtin_expect(x, 0)
#endif
