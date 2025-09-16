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

from gvrun.target import BuildParameter

def declare(target, container):

    BuildParameter(container, 'libc.printf', True, 'Include printf in libc')
    minimal = BuildParameter(container, 'libc.minimal', True, 'Include minimal libc')
    io_device = BuildParameter(container, 'libc.io_device', None, 'Device to be used for input/output in libc')

    if io_device is None:
        # If no device is specified, always take stdout for RTL as it is usally the only one
        # supported, and semihost for others to match the board on simulation platforms
        if container.get_parameter('/platform') == 'rtl':
            container.set_parameter('libc.io_device', 'stdout')
        else:
            container.set_parameter('libc.io_device', 'semihost')

    if minimal:
        container.add_subdirectory('minimal', target)
