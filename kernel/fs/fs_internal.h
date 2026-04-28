// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <pmsis/kernel/fs.h>


// FS drivers register their vtable via this symbol. Add new entries below as additional FS
// drivers (writefs, hostfs, ...) come online.
extern pi_vfs_mp_fs_api_t __pi_fs_readfs_api;
