# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

# PulpOS port of the Snitch cluster (the GVSoC snitch_v2 target, target name
# 'snitch'): the compute cores plus the data-mover core of the cluster, all
# running main() with the multicore parameter (core 0 only by default), I/O
# and exit through HTIF, the snRuntime API subset of
# pmsis/chips/snitch/snrt.h. Built with the pulp LLVM the latest Snitch cluster
# pins (SNITCH_LLVM, pulp-llvm-22). The Snitch testbench port is the
# pulpos.snitch.testbench module of this package.

from __future__ import annotations

import pulpos
from typing import Any, List, Tuple
import gvrun.target
from gvrun.parameter import BuildParameter
from pulpos.toolchain import RiscvLlvmToolchain, ToolchainConfig


# Per-core stack, at the end of the TCDM. Shared by the linker script (one slot
# per core) and crt0.S (which indexes them by mhartid with a shift).
_STACK_SIZE = 0x800

# The ISA extensions of the cluster cores the compiler is told about, as in
# the toolchain.mk the Snitch cluster generates (SN_RISCV_FEATURES)
_FEATURES = ['xssr', 'xfrep', 'xdma']


class SnitchClusterPulposModule(pulpos.PulposModule):

    def __init__(self, target: gvrun.target.SystemTreeNode, container: pulpos.SourceContainer):
        super().__init__(target, parent=container)

        self.add_define('CONFIG_CHIP_NAME', 'snitch_cluster')
        self.add_define('CONFIG_CHIP_FAMILY_NAME', 'snitch/cluster')
        self.add_define('CONFIG_CHIP_SNITCH_CLUSTER', '1')
        self.add_define('CONFIG_CYCLE_INC', '<arch/snitch/cluster/kernel/cycle.h>')

        attr = target.get_attributes()
        soc = attr.chip.soc
        cluster = soc.clusters[0]
        nb_core = int(cluster.nb_core)

        self.add_define('CONFIG_CLUSTER_TCDM_BASE', f'0x{int(cluster.tcdm.area.base):x}')
        self.add_define('CONFIG_CLUSTER_TCDM_SIZE', f'0x{int(cluster.tcdm.area.size):x}')
        self.add_define('CONFIG_CLUSTER_PERIPH_BASE', f'0x{int(cluster.peripheral.base):x}')
        self.add_define('CONFIG_CLUSTER_NB_CORE', f'{nb_core}')
        self.add_define('CONFIG_STACK_SIZE', hex(_STACK_SIZE))
        self.add_define('CONFIG_STACK_SIZE_LOG2', str(_STACK_SIZE.bit_length() - 1))

        # Run main() on every cluster core, like snRuntime: core 0 does the
        # runtime init, the others join it on the cluster hardware barrier.
        # OFF by default, the other cores are parked: main() must then
        # dispatch on the core index, and only core 0 reports the exit status.
        multicore = BuildParameter(self, 'multicore', False,
            'Run the program on every cluster core instead of parking the '
            'other ones.').value
        if multicore:
            self.add_define('CONFIG_SNITCH_MULTICORE', '1')

        linker_script = self.new_template_file('linker_script', 'link.ld',
            'arch/snitch/cluster/link.ld')
        linker_script.add_parameter('mem_start', f'0x{int(soc.hbm.base):x}')
        linker_script.add_parameter('mem_size', '0x01000000')
        linker_script.add_parameter('tcdm_start', f'0x{int(cluster.tcdm.area.base):x}')
        linker_script.add_parameter('tcdm_size', f'0x{int(cluster.tcdm.area.size):x}')
        linker_script.add_parameter('stack_size', hex(_STACK_SIZE * nb_core))
        self.add_ldflags([f'-T{linker_script.get_path()}'])

        path = pulpos.get_home(self)

        self.add_define('__RV32__', '1')

        # As the Snitch cluster toolchain.mk: no compressed instructions (the
        # cores do not have them), the Snitch extensions as target features,
        # and the vectorizers off (the cores have no vector unit, the compiler
        # would otherwise form vector code the cores do not decode).
        # FLEN of the cores: 64 (D extension) on the 64-bit cluster, 32 on the
        # FP32 one (cluster attribute fp32)
        has_double = 'd' in str(cluster.isa)[4:]
        march = 'rv32imafd_zifencei' if has_double else 'rv32imaf_zifencei'
        mabi = 'ilp32d' if has_double else 'ilp32f'
        features = []
        for feature in _FEATURES:
            features += ['-Xclang', '-target-feature', '-Xclang', f'+{feature}']
        self.add_cflags([
            f'-march={march}', f'-mabi={mabi}', '-mcmodel=medany',
            '-menable-experimental-extensions', *features,
            '-fno-builtin', '-fno-common',
            '-fno-vectorize', '-fno-slp-vectorize',
            '-mllvm', '-scalable-vectorization=off',
            '-mllvm', '-combiner-store-merging=false',
            '-mllvm', '-disable-vector-combine',
        ])
        self.add_ldflags([f'-march={march}', f'-mabi={mabi}', '-Wl,-z,norelro',
            '-fuse-ld=lld'])

        self.add_sources([
            'arch/snitch/cluster/kernel/crt0.S',
            'arch/snitch/cluster/kernel/hal.c',
            'arch/spatz/kernel/htif.c',
            'arch/spatz/kernel/div64.c',
        ])

        self.add_subdirectory(path, target)


class SnitchClusterPulposExecutable(pulpos.PulposExecutable):

    def __init__(self, name: str, target: gvrun.target.SystemTreeNode,
            parameters: list[tuple[str, Any]] | None=None):

        snitch_parameters: list[tuple[str, Any]] = [
            ('pulpos/kernel.threading', False),
            ('pulpos/kernel.event', False),
            # Replaced by the cluster one, which starts every core
            ('pulpos/crt0', False),
        ]

        if parameters is not None:
            snitch_parameters = snitch_parameters + parameters

        super().__init__(name, target, parameters=snitch_parameters)

        self.set_toolchain(RiscvLlvmToolchain(ToolchainConfig(path_from_env='SNITCH_LLVM')))

        self.pulpos = SnitchClusterPulposModule(target, container=self)


def new_executable(name, target,
        parameters:List[Tuple[str,Any]] | None=None):
    return SnitchClusterPulposExecutable(name, target, parameters=parameters)
