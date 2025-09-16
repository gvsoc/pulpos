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

 #include <stdio.h>
 #include <stdint.h>
 #include <kernel/hal.h>

 static char __pi_libc_buffer[PI_LIBC_PUTC_BUFFER_SIZE];
 static int __pi_libc_buffer_index = 0;

void __pi_init_soc()
{
}

int __pi_libc_fputc_safe(int c, FILE *stream)
{
    char *buffer = __pi_libc_buffer;
    int *index = &__pi_libc_buffer_index;

    buffer[*index] = c;
    *index = *index + 1;

    if (*index == PI_LIBC_PUTC_BUFFER_SIZE || c == '\n')
    {
        buffer[*index] = 0;

        __pi_libc_write(1, (uint8_t *)buffer, *index);
        *index = 0;
    }

    return 0;
}
