# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

"""PulpOS for SoftHier (pulp.chips.softhier and pulp.chips.softhier_v2).

SoftHier is a grid of Snitch + Spatz clusters connected by a NoC. Both
generations have the same software interface, and describe their
architecture in the board attributes this module reads (memory map, number
of clusters and cores).

The same binary runs on every cluster, from the instruction memory of the
cluster where it is loaded: core 0 of each cluster runs the runtime and
main, the other cores sleep unless the multicore option is on. Output goes
through the character register of each cluster, and the simulation stops
once every core reported its end of computation, with the OR of the
statuses as exit status. See arch/softhier/kernel/softhier.h for the
hardware helpers (position, remote TCDM, barriers).
"""

from __future__ import annotations

from typing import Any, List, Tuple

import gvrun.target
from gvrun.parameter import BuildParameter

import pulpos
from pulpos.toolchain import RiscvGccToolchain, RiscvLlvmToolchain, ToolchainConfig


# Per-core stack, in the cluster stack memory. Shared by the linker script
# (one slot per core) and crt0.S (which indexes them by mhartid).
_STACK_SIZE = 0x1000

# The TCDM top kept out of the .l1 section, for the global barrier counters
_TCDM_RESERVED = 0x40

# The march a test adds to its own cflags when it issues vector instructions
# from C: the default march has no 'v' so that the compiler never emits
# vector code on its own.
MARCH_VECTOR = 'rv32imafdv_zfh'


class SoftHierPulposModule(pulpos.PulposModule):

    def __init__(self, target: gvrun.target.SystemTreeNode, container: pulpos.SourceContainer,
            toolchain: str='gcc'):
        super().__init__(target, parent=container)

        self.add_define('CONFIG_CHIP_NAME', 'softhier')
        self.add_define('CONFIG_CHIP_FAMILY_NAME', 'softhier')
        self.add_define('CONFIG_CHIP_SOFTHIER', '1')
        self.add_define('CONFIG_CYCLE_INC', '<arch/softhier/kernel/cycle.h>')

        arch = target.get_attributes()

        # Hardware description, for the runtime and the tests
        nb_core = int(arch.num_core_per_cluster)
        self.add_define('CONFIG_SOFTHIER_NB_CLUSTER', f'{int(arch.num_cluster)}')
        self.add_define('CONFIG_SOFTHIER_NB_CORE_PER_CLUSTER', f'{nb_core}')
        self.add_define('CONFIG_SOFTHIER_TCDM_BASE', f'0x{int(arch.cluster_tcdm_base):x}')
        self.add_define('CONFIG_SOFTHIER_TCDM_SIZE', f'0x{int(arch.cluster_tcdm_size):x}')
        self.add_define('CONFIG_SOFTHIER_TCDM_REMOTE', f'0x{int(arch.cluster_tcdm_remote):x}')
        self.add_define('CONFIG_SOFTHIER_CLUSTER_REG_BASE', f'0x{int(arch.cluster_reg_base):x}')
        self.add_define('CONFIG_SOFTHIER_SOC_REG_BASE', f'0x{int(arch.soc_register_base):x}')
        self.add_define('CONFIG_SOFTHIER_SPATZ_NB_LANES', f'{int(arch.spatz_num_lane)}')
        self.add_define('CONFIG_STACK_SIZE', hex(_STACK_SIZE))
        # crt0 indexes the per-core stacks with a shift: Snitch has no scalar
        # multiplier
        self.add_define('CONFIG_STACK_SIZE_LOG2', str(_STACK_SIZE.bit_length() - 1))

        multicore = BuildParameter(self, 'multicore', False,
            'Run main on every core of the cluster instead of only on core 0. '
            'The cores then report the status returned by main.').value
        if multicore:
            self.add_define('CONFIG_SOFTHIER_MULTICORE', '1')

        if nb_core * _STACK_SIZE > int(arch.cluster_stack_size):
            raise RuntimeError(f'The cluster stack memory ({int(arch.cluster_stack_size):#x}) '
                f'cannot hold {nb_core} stacks of {_STACK_SIZE:#x}')

        BuildParameter(self, 'linker_script',  "link.ld", 'Linker script')

        if self.get_parameter('linker_script'):
            linker_script_template = f'arch/softhier/{self.get_parameter("linker_script")}'

            linker_script = self.new_template_file('linker_script', 'link.ld', linker_script_template)

            linker_script.add_parameter('mem_start', f'0x{int(arch.instruction_mem_base):x}')
            linker_script.add_parameter('mem_size', f'0x{int(arch.instruction_mem_size):x}')
            linker_script.add_parameter('stack_start', f'0x{int(arch.cluster_stack_base):x}')
            linker_script.add_parameter('stack_mem_size', f'0x{int(arch.cluster_stack_size):x}')
            linker_script.add_parameter('stack_size', hex(_STACK_SIZE * nb_core))
            linker_script.add_parameter('l1_start', f'0x{int(arch.cluster_tcdm_base):x}')
            linker_script.add_parameter('l1_size',
                f'0x{int(arch.cluster_tcdm_size) - _TCDM_RESERVED:x}')

            self.add_ldflags([
                f'-T{linker_script.get_path()}'
            ])

        path = pulpos.get_home(self)

        self.add_define('__RV32__', '1')

        # Neither 'v' (the compiler would emit vector code by itself, tests
        # issuing vector instructions add -march=MARCH_VECTOR to their own
        # cflags) nor 'c' (the cores do not implement it).
        march = BuildParameter(self, 'march', 'rv32imafd_zfh',
            'RISCV march used for compiling').value

        # -fno-builtin prevents the compiler from turning libc code patterns
        # into calls to functions the minimal libc does not provide.
        cflags = [f'-march={march}', '-mabi=ilp32d', '-fno-builtin']
        if toolchain == 'llvm':
            cflags += ['-mno-implicit-float', '-fno-vectorize', '-fno-slp-vectorize']
        self.add_cflags(cflags)

        self.add_define('CONFIG_SOFTHIER_MARCH_VECTOR', MARCH_VECTOR)

        link_flags = [f'-march={march}', '-mabi=ilp32d']
        if toolchain == 'llvm':
            link_flags += ['-Wl,-z,norelro', '-fuse-ld=lld']
        else:
            # Code and data share the instruction memory, in one RWX segment
            link_flags += ['-Wl,--no-warn-rwx-segments']
        self.add_ldflags(link_flags)

        # The 64-bit division and shift helpers replace the libgcc ones: the
        # toolchain only has libgcc with compressed instructions, which the
        # SoftHier cores do not implement (same helpers as Spatz).
        self.add_sources([
            'arch/softhier/kernel/crt0.S',
            'arch/softhier/kernel/hal.c',
            'arch/spatz/kernel/div64.c',
        ])

        self.add_subdirectory(path, target)


class SoftHierPulposExecutable(pulpos.PulposExecutable):

    def __init__(self, name: str, target: gvrun.target.SystemTreeNode,
            parameters: list[tuple[str, Any]] | None=None):

        softhier_parameters: list[tuple[str, Any]] = [
            # No interrupt controller nor timer on SoftHier
            ('pulpos/kernel.threading', False),
            ('pulpos/kernel.event', False),
            # The generic crt0 is replaced by the SoftHier one, which handles
            # the other cores
            ('pulpos/crt0', False),
        ]

        if parameters is not None:
            softhier_parameters = softhier_parameters + parameters

        super().__init__(name, target, parameters=softhier_parameters)

        # The SoftHier toolchain (see pulp/chips/softhier/softhier.mk,
        # third_party/toolchain) is a GCC with the V extension.
        toolchain = BuildParameter(self, 'toolchain',  "gcc",
            'Toolchain to be used for compiling and linking').value

        if toolchain == 'gcc':
            config = ToolchainConfig(path_from_env='SOFTHIER_GCC')
            self.set_toolchain(RiscvGccToolchain(config))
        elif toolchain == 'llvm':
            config = ToolchainConfig(path_from_env='SOFTHIER_LLVM')
            self.set_toolchain(RiscvLlvmToolchain(config))

        self.pulpos = SoftHierPulposModule(target, container=self, toolchain=toolchain)


def new_executable(name, target,
        parameters:List[Tuple[str,Any]] | None=None):
    return SoftHierPulposExecutable(name, target, parameters=parameters)
