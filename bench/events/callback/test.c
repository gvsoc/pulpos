// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <stdio.h>
#include <arch/gap/gap9/kernel/perf.h>

int main()
{
    pi_perf_enable(1 << PI_PERF_ACTIVE_CYCLES);

    pi_perf_start();
    printf("Hello\n");
    pi_perf_stop();

    printf("Active cycles: %d\n", pi_perf_read(PI_PERF_ACTIVE_CYCLES));

    return 0;
}
