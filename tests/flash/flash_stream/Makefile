# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

APP       = flash_stream
APP_SRCS += test.c

# Short variant, for slow platforms (RTL): 1 KB stream instead of 8 KB
ifeq ($(SHORT),1)
APP_CFLAGS += -DTEST_SHORT=1
endif

# Match the gvrun flow (Gap9PulposExecutable), which enables the allocator
# and the filesystem for every executable.
CONFIG_GAP9_BUILD_PULPOS_SW_KERNEL_SW_ALLOC := 1
CONFIG_GAP9_BUILD_PULPOS_SW_KERNEL_SW_FS := 1

include $(PULPOS_HOME)/rules/pulpos.mk
