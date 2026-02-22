#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

from gvrun.parameter import BuildParameter
from pulpos import SourceContainer

def declare(target, container: SourceContainer):

    crt0 = BuildParameter(container, 'crt0', True, 'Add crt0').value
    if crt0:
        container.add_sources(['kernel/crt0.S'])

    threading = BuildParameter(container, 'kernel.threading', True, 'Enable thread scheduling').value
    if threading:
        preemption = BuildParameter(container, 'kernel.threading.preemption', True, 'Enable thread scheduling preemption').value
        slice = BuildParameter(container, 'kernel.threading.slice', 1000, 'Thread slice duration in microseconds').value

        if preemption:
            container.add_define('CONFIG_THREAD_PREEMPTION', 1)
            container.add_define('CONFIG_THREAD_SLICE', slice)

        container.add_define('CONFIG_THREAD', 1)

        container.add_sources([
            'kernel/thread.c',
            'kernel/thread_asm.S',
        ])

    event = BuildParameter(container, 'kernel.event', True, 'Enable events').value
    if event:
        container.add_define('CONFIG_EVENT', 1)

        container.add_sources([
            'kernel/event.c',
            'kernel/event_asm.S',
        ])

    container.add_sources([
        'kernel/init.c',
    ])

    container.add_cflags('-fno-tree-loop-distribute-patterns')
