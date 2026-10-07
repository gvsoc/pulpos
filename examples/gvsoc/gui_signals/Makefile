# SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
#
# SPDX-License-Identifier: Apache-2.0

include $(PULPOS_HOME)/rules/pulpos_config.mk

APP       = test
APP_SRCS += test.c

# The signals of the application, one per kind of display, shown in the GUI under the app group
# (gvrun --help for the --vcd-trace format). Same as config.py for the gvrun flow.
APP_RUN_OPTIONS += --vcd-trace=app/kernel:type=string:display=string_box
APP_RUN_OPTIONS += --vcd-trace=app/kernel_id:display=state_box:labels=0=-,1=load@blue,2=conv@green,3=relu@orange,4=store@red
APP_RUN_OPTIONS += --vcd-trace=app/frame:display=box:format=dec
APP_RUN_OPTIONS += --vcd-trace=app/busy:display=logic_box:message=BUSY
APP_RUN_OPTIONS += --vcd-trace=app/dma:display=pulse
APP_RUN_OPTIONS += --vcd-trace=app/fill:type=real:display=analog
APP_RUN_OPTIONS += --vcd-trace=app/macs:display=box:format=dec:aggregation=sum
APP_RUN_OPTIONS += --vcd-trace=app/log:type=string:display=string

include $(PULPOS_HOME)/rules/pulpos.mk
