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
from gvrun.target import BuildParameter


def declare(target, container):
    declare_folders(target, container)

    declare_flags(target, container)

    declare_log(target, container)

    for subdir in ['kernel', 'lib']:
        container.add_subdirectory(subdir, target)


def declare_folders(target, container):
    home = os.environ.get('PULPOS_HOME', '')

    container.add_includes([
        os.path.join(home, 'include'),
        home
    ])

    container.add_source_path(os.environ.get('PULPOS_HOME'))


def declare_log(target, container):
    BuildParameter(container, 'log', [], 'Logs to be enabled')
    BuildParameter(container, 'log.all', False, 'Enable all logs')
    BuildParameter(container, 'log.level', 'error', 'Log level')

    for log in container.get_parameter('log'):
        container.add_define(f'CONFIG_LOG_{log.upper()}', 1)

    if container.get_parameter('log.all'):
        container.add_define('CONFIG_LOG_ALL', 1)

    if container.get_parameter('log.level'):
        container.add_define('CONFIG_LOG_LEVEL', f'PI_LOG_{container.get_parameter("log.level").upper()}')


def declare_flags(target, container):
    platform = container.get_parameter('/platform')

    container.add_define(f'__PLATFORM_{platform.upper()}__', '1')

    container.add_cflags([
        '-fdata-sections', '-ffunction-sections', '-fno-jump-tables'
    ])

    container.add_ldflags([
       '-Wl,--gc-sections', '-fno-eliminate-unused-debug-symbols', '-nostdlib'
    ])

    if container._get_toolchain().get_family() not in ['llvm']:
        container.add_ldflags([
            '-lgcc', '-nostartfiles'
        ])
    else:
        container.add_ldflags([
            '-mno-relax'
        ])
