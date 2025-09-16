    #!/usr/bin/env python3

#
# Copyright (C) 2025 GreenWaves Technologies, ETH Zurich and University of Bologna
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# Authors: Germain Haugou (germain.haugou@gmail.com)
#

import os

def declare(target, container):

    if container.get_parameter('libc.io_device') is not None:
        container.add_define(f'CONFIG_LIBC_IO_{container.get_parameter("libc.io_device").upper()}', 1)

    container.add_includes([
        os.path.join(os.environ.get('PULPOS_HOME'), 'include/pmsis/lib/libc/minimal'),
    ])

    container.add_define('CONFIG_LIBC_MINIMAL', 1)

    container.add_sources([
        'lib/libc/minimal/io.c',
        'lib/libc/minimal/string.c',
    ])

    if container.get_parameter('libc.printf'):
        container.add_sources([
            'lib/libc/minimal/prf.c',
            'lib/libc/minimal/fprintf.c',
            'lib/libc/minimal/sprintf.c',
        ])
