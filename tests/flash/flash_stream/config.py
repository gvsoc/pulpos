#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

import pulpos
from gvrun.parameter import BuildParameter

def declare(target):

    test = pulpos.new_executable('test', target)

    test.set_optimization_level('-Os -g')
    test.add_sources('test.c')
    # Short variant, for slow platforms (RTL): 1 KB stream instead of 8 KB
    if BuildParameter(test, 'short', False, 'Short variant of the test, for RTL').value:
        test.add_define('TEST_SHORT', '1')
