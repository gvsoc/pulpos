// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// Multi-cluster SoftHier check: every cluster fills a TCDM buffer with a
// pattern of its own, then every cluster reads the buffer of the next one
// through the NoC, between global barriers. Also checks that the data is
// private to each cluster. The status returned by main is the number of
// errors, which reaches the exit status of the simulation.
//
// Runs on one core per cluster, or on every core with the multicore option
// (then each core handles its own slice of the buffer).

#include <stdio.h>
#include <stdint.h>
#include <arch/softhier/kernel/softhier.h>

#define NB_WORDS 256

static uint32_t buffer[NB_WORDS] __attribute__((section(".l1")));

// In the instruction memory of each cluster: each cluster has its own copy
static uint32_t cluster_data = 0x1234;

static uint32_t pattern(uint32_t cid, uint32_t i)
{
    return (cid << 16) ^ (i * 2654435761u);
}

int main()
{
    uint32_t cid = pi_softhier_cluster_id();
    uint32_t core = pi_softhier_core_id();
    uint32_t nb_cluster = pi_softhier_nb_cluster();
#ifdef CONFIG_SOFTHIER_MULTICORE
    uint32_t nb_core = pi_softhier_nb_core();
#else
    uint32_t nb_core = 1;
#endif
    uint32_t slice = NB_WORDS / nb_core;
    uint32_t first = core * slice;
    int errors = 0;

    for (uint32_t i = first; i < first + slice; i++)
    {
        buffer[i] = pattern(cid, i);
    }

    if (core == 0)
    {
        cluster_data += cid;
    }

    pi_softhier_global_barrier();

    uint32_t next = (cid + 1) % nb_cluster;
    volatile uint32_t *remote = (volatile uint32_t *)pi_softhier_remote_tcdm(next,
        (uintptr_t)buffer - CONFIG_SOFTHIER_TCDM_BASE);
    for (uint32_t i = first; i < first + slice; i++)
    {
        if (remote[i] != pattern(next, i))
        {
            errors++;
        }
    }

    if (cluster_data != 0x1234 + cid)
    {
        errors++;
    }

    pi_softhier_global_barrier();

    if (cid == 0 && core == 0)
    {
        printf("SoftHier: %d clusters, %d cores per cluster, %d running\n",
            (int)nb_cluster, (int)pi_softhier_nb_core(), (int)nb_core);
    }

    printf("Cluster %d core %d: %s\n", (int)cid, (int)core, errors ? "KO" : "OK");

    return errors;
}
