# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0
#
# Authors: Germain Haugou (germain.haugou@gmail.com)

from __future__ import annotations

import pulpos
from typing import Any, List, Tuple
import gvrun.target
from gvrun.parameter import BuildParameter
from pulpos.toolchain import RiscvGccToolchain, RiscvLlvmToolchain, ToolchainConfig


class SpatzPulposModule(pulpos.PulposModule):

    def __init__(self, target: gvrun.target.SystemTreeNode, container: pulpos.SourceContainer,
            toolchain: str='llvm'):
        super().__init__(target, parent=container)

        self.add_define('CONFIG_CHIP_NAME', 'spatz')
        self.add_define('CONFIG_CHIP_FAMILY_NAME', 'spatz')
        self.add_define('CONFIG_CHIP_SPATZ', '1')
        self.add_define('CONFIG_CYCLE_INC', '<arch/spatz/kernel/cycle.h>')

        attr = target.get_attributes()
        soc = attr.chip.soc
        cluster = soc.clusters[0]

        # Expose the cluster characteristics to the code, mostly for tests
        # which need to place buffers in the TCDM
        self.add_define('CONFIG_CLUSTER_TCDM_BASE', f'0x{int(cluster.tcdm.area.base):x}')
        self.add_define('CONFIG_CLUSTER_TCDM_SIZE', f'0x{int(cluster.tcdm.area.size):x}')
        self.add_define('CONFIG_CLUSTER_PERIPH_BASE', f'0x{int(cluster.peripheral.base):x}')
        self.add_define('CONFIG_CLUSTER_NB_CORE', f'{int(cluster.nb_core)}')
        self.add_define('CONFIG_SPATZ_NB_LANES', f'{int(cluster.spatz_nb_lanes)}')

        BuildParameter(self, 'linker_script',  "link.ld", 'Linker script')

        if self.get_parameter('linker_script'):
            linker_script_template = f'arch/spatz/{self.get_parameter("linker_script")}'

            linker_script = self.new_template_file('linker_script', 'link.ld', linker_script_template)

            # The program is placed in HBM, which is modeled with zero latency
            # on the spatz board. The TCDM is kept free for test buffers since
            # the VLSU ports are directly connected to it.
            linker_script.add_parameter('mem_start', f'0x{int(soc.hbm.base):x}')
            linker_script.add_parameter('mem_size', '0x01000000')

            self.add_ldflags([
                f'-T{linker_script.get_path()}'
            ])

        path = pulpos.get_home(self)

        self.add_define('__RV32__', '1')

        # The RTL spatz cluster does not support the compressed extension,
        # march can be overriden to rv32imafdv when compiling for it.
        # The gcc toolchain is too old to support the vector extension, only
        # scalar code can be compiled with it.
        default_march = 'rv32imafdcv' if toolchain == 'llvm' else 'rv32imafdc'
        march = BuildParameter(self, 'march', default_march, 'RISCV march used for compiling').value

        # -fno-builtin prevents the compiler from turning libc code patterns
        # into calls to functions the minimal libc does not provide, like
        # memchr. -z norelro prevents lld from complaining about the got
        # section placement.
        #
        # -fno-vectorize / -fno-slp-vectorize: `v` is in the march because the
        # tests issue vector instructions on purpose, but that also lets the
        # auto-vectorizer rewrite ordinary scalar loops as RVV. It reaches for
        # instructions the GVSoC model does not decode -- clang 22 turns a
        # plain `buf[i] = i` into `vsetvli` + `vid.v`, which is not in
        # isa_rvv_timed.py (nor is the rest of the VMUNARY0 family) -- and the
        # illegal instruction traps into the bootrom's park loop, so the test
        # hangs with no output at all. Explicit vector code is unaffected;
        # only the compiler's own vectorization is turned off.
        self.add_cflags([
            f'-march={march}', '-mabi=ilp32d', '-fno-builtin',
            '-fno-vectorize', '-fno-slp-vectorize'
        ])
        # -fuse-ld=lld has to be explicit: the clang driver otherwise falls
        # back to the host `ld`, which rejects the riscv emulation mode
        # ("unrecognised emulation mode: elf32lriscv"). Older spatz clang
        # builds defaulted to lld and hid this.
        link_flags = [f'-march={march}', '-mabi=ilp32d', '-Wl,-z,norelro']
        if toolchain == 'llvm':
            link_flags.append('-fuse-ld=lld')
        self.add_ldflags(link_flags)

        self.add_sources([
            'arch/spatz/kernel/crt0.S',
            'arch/spatz/kernel/hal.c',
            'arch/spatz/kernel/div64.c',
        ])

        # Host I/O and exit go through semihosting by default. On RTL
        # platforms, where the binary is loaded through the RISC-V frontend
        # server (fesvr), the HTIF interface must be used instead.
        htif = BuildParameter(self, 'htif', False,
            'Use the HTIF host interface (tohost/fromhost) instead of semihosting for host '
            'I/O and exit. Required when running on RTL platforms.').value
        if htif:
            self.add_define('CONFIG_SPATZ_HTIF', '1')
            self.add_sources(['arch/spatz/kernel/htif.c'])

        self.add_subdirectory(path, target)



class SpatzPulposExecutable(pulpos.PulposExecutable):


    def __init__(self, name: str, target: gvrun.target.SystemTreeNode,
            parameters: list[tuple[str, Any]] | None=None):

        spatz_parameters: list[tuple[str, Any]] = [
            ('pulpos/kernel.threading', False),
            ('pulpos/kernel.event', False),
            # The generic crt0 is replaced by a spatz-specific one which parks
            # the secondary harts since all the cluster cores are started
            # through the bootrom
            ('pulpos/crt0', False),
        ]

        if parameters is not None:
            spatz_parameters = spatz_parameters + parameters

        super().__init__(name, target, parameters=spatz_parameters)


        toolchain = BuildParameter(self, 'toolchain',  "llvm", 'Toolchain to be used for compiling and linking').value

        if toolchain == 'gcc':
            config = ToolchainConfig(path_from_env='SPATZ_GCC')
            self.set_toolchain(RiscvGccToolchain(config))
        elif toolchain == 'llvm':
            config = ToolchainConfig(path_from_env='SPATZ_LLVM')
            self.set_toolchain(RiscvLlvmToolchain(config))

        self.pulpos = SpatzPulposModule(target, container=self, toolchain=toolchain)


def new_executable(name, target,
        parameters:List[Tuple[str,Any]] | None=None):
    return SpatzPulposExecutable(name, target, parameters=parameters)
