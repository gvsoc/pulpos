// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <pmsis/chips/pulp/cluster.h>

PI_BSP_CLUSTER_DECL(pi_bsp_cluster_0);

#define PI_BSP_CLUSTER(x) ((pi_device_t *)(&pi_bsp_cluster_##x))
