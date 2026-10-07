#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

import pulpos
import gvsoc.gui

def declare(target):

    test = pulpos.new_executable('test', target)

    test.set_optimization_level('-Os -g')
    test.add_sources('test.c')

    # The signals of the application, one per kind of display, shown in the GUI under the app
    # group. Same as the --vcd-trace options of the Makefile (make flow).
    target.add_vcd_trace('app/kernel', 'string', gui='app/kernel',
        display=gvsoc.gui.DisplayStringBox())
    target.add_vcd_trace('app/kernel_id', 'int', gui='app/kernel_id',
        display=gvsoc.gui.DisplayStateBox({0: None, 1: ('load', 'blue'), 2: ('conv', 'green'),
            3: ('relu', 'orange'), 4: ('store', 'red')}))
    target.add_vcd_trace('app/frame', 'int', gui='app/frame',
        display=gvsoc.gui.DisplayBox(format='dec'))
    target.add_vcd_trace('app/busy', 'int', gui='app/busy',
        display=gvsoc.gui.DisplayLogicBox('BUSY'))
    target.add_vcd_trace('app/dma', 'int', gui='app/dma',
        display=gvsoc.gui.DisplayPulse())
    target.add_vcd_trace('app/fill', 'real', gui='app/fill',
        display=gvsoc.gui.DisplayAnalog())
    target.add_vcd_trace('app/macs', 'int', gui='app/macs',
        display=gvsoc.gui.DisplayBox(format='dec', aggregation='sum'))
    target.add_vcd_trace('app/log', 'string', gui='app/log',
        display=gvsoc.gui.DisplayString())
