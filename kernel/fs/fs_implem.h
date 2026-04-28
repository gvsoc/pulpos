// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <pmsis/kernel/fs.h>


// Read pass-through: the active FS driver issues a single async flash read straight against
// the user-supplied event header, so this is a trivial vtable dispatch.
ALWAYS_INLINE void pi_fs_read_async(pi_fs_file_t *file, void *ptr, size_t size, pi_fs_evt_t *event)
{
    file->api->read(file->instance, file, ptr, size, event);
}
