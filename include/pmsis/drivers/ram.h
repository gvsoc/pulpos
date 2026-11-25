// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// Generic external-RAM driver API, mirroring the flash API
// (<pmsis/drivers/flash.h>). It provides the device-independent entry points
// (open / close / alloc / free) which dispatch to a concrete RAM driver through
// a function-pointer vtable embedded as the first member of the device object.
//
// The copy entry points (pi_ram_read / pi_ram_write and their 2D and
// asynchronous variants) move data between the processor memory and a RAM
// which is not memory-mapped, like an OctoSPI PSRAM. A RAM which is reached
// directly (e.g. by the cluster mchan through its memory-mapped address)
// leaves them unimplemented (NULL copy_2d) and only hands out addresses.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <pmsis/kernel/kernel.h>
#include <pmsis/kernel/event.h>


#ifndef PI_RAM_EVT_PRIV_WORDS
#define PI_RAM_EVT_PRIV_WORDS 6
#endif

/** @brief Event of an asynchronous RAM copy.
 *
 * A pi_evt_t (initialized as usual, e.g. with pi_evt_sig_init) followed by
 * words the driver uses to queue the copy until it can execute it.
 */
typedef struct pi_ram_evt_s
{
    pi_evt_t header;
    uint32_t priv[PI_RAM_EVT_PRIV_WORDS];
} pi_ram_evt_t;


/** @brief RAM driver method table.
 *
 * A concrete RAM device object embeds this as its first member so the generic
 * ``pi_ram_*`` entry points can recover it from the ``pi_device_t`` handle.
 */
typedef struct pi_ram_api_s
{
    int  (*open) (pi_device_t *device);
    void (*close)(pi_device_t *device);
    int  (*alloc)(pi_device_t *device, uint32_t *addr, uint32_t size);
    int  (*free) (pi_device_t *device, uint32_t addr, uint32_t size);
    // Copy between the RAM (addr) and the processor memory (data), called
    // with interrupts disabled. ext2loc is 1 for a read from the RAM. The
    // copy is 2D on the RAM side: lines of length bytes, stride bytes apart,
    // packed in the processor memory (stride == length for a 1D copy). NULL
    // for a RAM which is only reached directly.
    void (*copy_2d)(pi_device_t *device, uint32_t addr, void *data, uint32_t size,
        uint32_t stride, uint32_t length, int ext2loc, pi_ram_evt_t *event);
} pi_ram_api_t;


/** @brief Base RAM configuration.
 *
 * Concrete device configurations (e.g. ``struct pi_apsram_conf``) start with
 * the device-specific fields; this base type only exists for API parity with
 * the other drivers.
 */
struct pi_ram_conf
{
    pi_ram_api_t *api;
};


/** @brief Open a RAM device.
 *
 * Must be called before the device is used. For a directly-addressable RAM this
 * only sets up the host-side allocator; it does not touch the RAM itself.
 *
 * @param device  Device handle (a concrete RAM device whose first member is a
 *                ``pi_ram_api_t``).
 * @return 0 on success, -1 on error.
 */
ALWAYS_INLINE int pi_ram_open(pi_device_t *device)
{
    return ((pi_ram_api_t *)device)->open(device);
}


/** @brief Close an opened RAM device and release its host-side resources. */
ALWAYS_INLINE void pi_ram_close(pi_device_t *device)
{
    ((pi_ram_api_t *)device)->close(device);
}


/** @brief Allocate a chunk of RAM.
 *
 * @param device  The RAM device.
 * @param addr    Filled with the allocated RAM address on success.
 * @param size    Number of bytes to allocate.
 * @return 0 on success, -1 if the allocation failed.
 */
ALWAYS_INLINE int pi_ram_alloc(pi_device_t *device, uint32_t *addr, uint32_t size)
{
    return ((pi_ram_api_t *)device)->alloc(device, addr, size);
}


/** @brief Free a chunk previously returned by ``pi_ram_alloc``.
 *
 * As with the other PulpOS allocators, the size must be passed back explicitly.
 *
 * @param device  The RAM device.
 * @param addr    The RAM address to free.
 * @param size    The size that was allocated.
 * @return 0 on success, -1 on error.
 */
ALWAYS_INLINE int pi_ram_free(pi_device_t *device, uint32_t addr, uint32_t size)
{
    return ((pi_ram_api_t *)device)->free(device, addr, size);
}


/** @brief Copy from the RAM to the processor memory asynchronously.
 *
 * @param device  The RAM device.
 * @param addr    The address of the copy in the RAM.
 * @param data    The address of the copy in the processor memory.
 * @param size    The size in bytes of the copy.
 * @param event   Notified once the copy is over.
 */
ALWAYS_INLINE void pi_ram_read_async(pi_device_t *device, uint32_t addr, void *data,
    uint32_t size, pi_ram_evt_t *event)
{
    int irq = pi_irq_lock();
    ((pi_ram_api_t *)device)->copy_2d(device, addr, data, size, size, size, 1, event);
    pi_irq_unlock(irq);
}


/** @brief Copy from the processor memory to the RAM asynchronously.
 *
 * Same as pi_ram_read_async, in the other direction.
 */
ALWAYS_INLINE void pi_ram_write_async(pi_device_t *device, uint32_t addr, void *data,
    uint32_t size, pi_ram_evt_t *event)
{
    int irq = pi_irq_lock();
    ((pi_ram_api_t *)device)->copy_2d(device, addr, data, size, size, size, 0, event);
    pi_irq_unlock(irq);
}


/** @brief 2D copy from the RAM to the processor memory asynchronously.
 *
 * The RAM side is a rectangle area: lines of length bytes, whose beginnings
 * are stride bytes apart. The lines are packed in the processor memory.
 *
 * @param device  The RAM device.
 * @param addr    The address of the copy in the RAM.
 * @param data    The address of the copy in the processor memory.
 * @param size    The size in bytes of the copy.
 * @param stride  The distance in bytes between 2 line beginnings in the RAM.
 * @param length  The length in bytes of a line.
 * @param event   Notified once the copy is over.
 */
ALWAYS_INLINE void pi_ram_read_2d_async(pi_device_t *device, uint32_t addr, void *data,
    uint32_t size, uint32_t stride, uint32_t length, pi_ram_evt_t *event)
{
    int irq = pi_irq_lock();
    ((pi_ram_api_t *)device)->copy_2d(device, addr, data, size, stride, length, 1, event);
    pi_irq_unlock(irq);
}


/** @brief 2D copy from the processor memory to the RAM asynchronously.
 *
 * Same as pi_ram_read_2d_async, in the other direction.
 */
ALWAYS_INLINE void pi_ram_write_2d_async(pi_device_t *device, uint32_t addr, void *data,
    uint32_t size, uint32_t stride, uint32_t length, pi_ram_evt_t *event)
{
    int irq = pi_irq_lock();
    ((pi_ram_api_t *)device)->copy_2d(device, addr, data, size, stride, length, 0, event);
    pi_irq_unlock(irq);
}


/** @brief Copy from the RAM to the processor memory.
 *
 * The caller is blocked until the copy is over.
 */
ALWAYS_INLINE void pi_ram_read(pi_device_t *device, uint32_t addr, void *data, uint32_t size)
{
    pi_ram_evt_t event;
    pi_ram_read_async(device, addr, data, size, (pi_ram_evt_t *)pi_evt_sig_init(&event.header));
    pi_evt_sig_wait(&event.header);
}


/** @brief Copy from the processor memory to the RAM.
 *
 * The caller is blocked until the copy is over.
 */
ALWAYS_INLINE void pi_ram_write(pi_device_t *device, uint32_t addr, void *data, uint32_t size)
{
    pi_ram_evt_t event;
    pi_ram_write_async(device, addr, data, size, (pi_ram_evt_t *)pi_evt_sig_init(&event.header));
    pi_evt_sig_wait(&event.header);
}


/** @brief 2D copy from the RAM to the processor memory.
 *
 * The caller is blocked until the copy is over.
 */
ALWAYS_INLINE void pi_ram_read_2d(pi_device_t *device, uint32_t addr, void *data,
    uint32_t size, uint32_t stride, uint32_t length)
{
    pi_ram_evt_t event;
    pi_ram_read_2d_async(device, addr, data, size, stride, length,
        (pi_ram_evt_t *)pi_evt_sig_init(&event.header));
    pi_evt_sig_wait(&event.header);
}


/** @brief 2D copy from the processor memory to the RAM.
 *
 * The caller is blocked until the copy is over.
 */
ALWAYS_INLINE void pi_ram_write_2d(pi_device_t *device, uint32_t addr, void *data,
    uint32_t size, uint32_t stride, uint32_t length)
{
    pi_ram_evt_t event;
    pi_ram_write_2d_async(device, addr, data, size, stride, length,
        (pi_ram_evt_t *)pi_evt_sig_init(&event.header));
    pi_evt_sig_wait(&event.header);
}


#ifdef __cplusplus
}
#endif
