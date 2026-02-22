#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

import os
from gvrun.parameter import BuildParameter


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

    if platform == 'gvsoc':
        container.add_includes(f'{os.environ.get("GVSOC_HOME")}/include/target')

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
