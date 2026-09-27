# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

import pulpos


def declare(target):

    test = pulpos.new_executable('test', target)

    test.set_optimization_level('-Os -g')
    test.add_sources('test.c')
