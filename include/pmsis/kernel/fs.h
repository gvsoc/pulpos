// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#pragma once

#include <stdint.h>
#include <unistd.h>
#include <pmsis/kernel/kernel.h>
#include <pmsis/drivers/flash.h>
#include <pmsis/kernel/event.h>


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup file_system_api
 * @{
 */


// Open mode flags. Only read is supported by the readfs driver.
#define PI_FS_O_READ  0x01

typedef uint32_t pi_fs_mode_t;
typedef struct pi_vfs_s pi_vfs_t;
typedef struct pi_fs_file_s pi_fs_file_t;
typedef struct pi_vfs_mp_s pi_vfs_mp_t;


/**
 * @brief File or directory information returned by ``pi_fs_stat``.
 */
struct pi_fs_dirent
{
    size_t size;
};


/**
 * @brief Async event used by the FS API.
 *
 * Wraps a ``pi_evt_t`` (the user inits it as a signal or callback event) plus a queue link the
 * VFS uses to defer pending operations.
 */
typedef struct pi_fs_evt_s
{
    pi_evt_t header;
    struct pi_fs_evt_s *next;
} pi_fs_evt_t;


/** @brief Open a file (sync). */
int pi_fs_open(pi_vfs_t *vfs, pi_fs_file_t *file, const char *file_name, pi_fs_mode_t flags);

/** @brief Open a file (async). The completion event must be signal- or callback-initialised. */
void pi_fs_open_async(pi_vfs_t *vfs, pi_fs_file_t *file, const char *file_name,
                      pi_fs_mode_t flags, pi_fs_evt_t *event);

/** @brief Close a file (sync). */
int pi_fs_close(pi_vfs_t *vfs, pi_fs_file_t *file);

/** @brief Close a file (async). */
void pi_fs_close_async(pi_vfs_t *vfs, pi_fs_file_t *file, pi_fs_evt_t *event);

/** @brief Read up to ``size`` bytes from the current position; returns the number actually read. */
ssize_t pi_fs_read(pi_fs_file_t *file, void *ptr, size_t size);

/** @brief Set the absolute read position. Local-only (no flash IO); returns 0 on success. */
int pi_fs_seek(pi_fs_file_t *file, size_t offset);

/** @brief Read (async). Status returns the byte count on completion. */
ALWAYS_INLINE void pi_fs_read_async(pi_fs_file_t *file, void *ptr, size_t size,
                                    pi_fs_evt_t *event);

/** @brief Stat a file. Fills ``entry->size``. Returns 0 on success, -1 if the file is missing. */
int pi_fs_stat(pi_vfs_t *vfs, const char *path, struct pi_fs_dirent *entry);

/** @brief Stat (async). */
void pi_fs_stat_async(pi_vfs_t *vfs, const char *path, struct pi_fs_dirent *entry,
                      pi_fs_evt_t *event);

/** @brief Release every flash and FS instance the VFS has lazily mounted. */
int pi_fs_flush(pi_vfs_t *vfs);

/** @brief Flush (async). */
void pi_fs_flush_async(pi_vfs_t *vfs, pi_fs_evt_t *event);

/**
 * @}
 */


/**
 * @addtogroup file_system_devtree_macros
 * @{
 */

/** @brief Forward-declare a VFS instance defined elsewhere with ``PI_BSP_VFS_INST``. */
#define PI_BSP_VFS_DECL(instance_name) \
    extern pi_vfs_t instance_name

/**
 * @brief Define a VFS instance with one or more mount points.
 *
 * Example:
 * @code
 * PI_BSP_VFS_INST(my_vfs, {
 *     PI_BSP_VFS_MOUNT_POINT("/mram", &mram_dev),
 * });
 * @endcode
 */
#define PI_BSP_VFS_INST(instance_name, user_mount_points) \
    static pi_vfs_mp_t __pi_bsp_vfs_mps_##instance_name[] = user_mount_points; \
    pi_vfs_t instance_name = { \
        .nb_mount_points = sizeof(__pi_bsp_vfs_mps_##instance_name) / sizeof(pi_vfs_mp_t), \
        .mount_points    = __pi_bsp_vfs_mps_##instance_name, \
        .waiting_first   = NULL, \
        .waiting_last    = NULL, \
    }

/** @brief One mount-point entry inside ``PI_BSP_VFS_INST``. */
#define PI_BSP_VFS_MOUNT_POINT(user_path, user_flash) \
    { .path = user_path, .flash = (pi_device_t *)user_flash, .opened = 0, .flash_opened = 0 }

/**
 * @}
 */

#include <kernel/fs/fs_data.h>
#include <kernel/fs/fs_implem.h>


#ifdef __cplusplus
}
#endif
