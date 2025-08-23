#
# Copyright (C) 2025 Germain Haugou
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
import pulpos
from typing import cast
import gvrun.target
import snitch_testbench
from gvrun.target import BuildParameter
from pulpos.toolchain import RiscvGccToolchain, RiscvLlvmToolchain, ToolchainConfig

class SnitchTestbenchPulposModule(pulpos.PulposModule):

    def __init__(self, target: gvrun.target.SystemTreeNode, container: pulpos._SourceContainer):
        super().__init__(target, parent=container)

        self.add_define('CONFIG_CHIP_NAME', 'snitch/testbench')
        self.add_define('CONFIG_CHIP_FAMILY_NAME', 'snitch/testbench')
        self.add_define('CONFIG_CHIP_SNITCH_TESTBENCH', '1')

        attr = cast(snitch_testbench.SnitchTestbenchAttr, target.get_attributes())

        BuildParameter(self, 'linker_script',  "link.ld", 'Linker script')

        if self.get_parameter('linker_script'):
            linker_script_template = f'chips/snitch/testbench/{self.get_parameter("linker_script")}'

            linker_script = self.new_template_file('linker_script', 'link.ld', linker_script_template)

            linker_script.add_parameter('mem_start', attr.mem_l0.base)
            linker_script.add_parameter('mem_size', attr.mem_l0.size)

            self.add_ldflags([
                f'-T{linker_script.get_path()}'
            ])

        path = pulpos.get_home(self)

        self.add_define('__RV32__', '1')

        self.add_cflags([
            '-march=rv32imafdc'
        ])
        self.add_ldflags([
            '-march=rv32imafdc'
        ])

        self.add_sources(['chips/snitch/testbench/kernel/hal.c'])

        self.add_subdirectory(path, target)



class SnitchTestbenchPulposExecutable(pulpos.PulposExecutable):

    def __init__(self, name, target):
        super().__init__(name, target)

        toolchain = BuildParameter(self, 'toolchain',  "gcc", 'Toolchain to be used for compiling and linking').value

        if toolchain == 'gcc':
            config = ToolchainConfig(path_from_env='SNITCH_GCC_TOOLCHAIN')
            self.set_toolchain(RiscvGccToolchain(config))
        elif toolchain == 'llvm':
            config = ToolchainConfig(path_from_env='SNITCH_LLVM_TOOLCHAIN')
            self.set_toolchain(RiscvLlvmToolchain(config))

        self.pulpos = SnitchTestbenchPulposModule(target, container=self)


def new_executable(name, target):
    return SnitchTestbenchPulposExecutable(name, target)
