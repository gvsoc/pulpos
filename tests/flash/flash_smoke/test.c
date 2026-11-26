// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

// Minimal flash-API smoke test. The flow (open -> get_info -> close) uses
// only the generic `pmsis/drivers/flash.h` API and should work against any
// concrete flash driver. The device is the default flash of the board
// (PI_BSP_FLASH).

#include <stdio.h>
#include <stddef.h>
#include <pmsis/drivers/flash.h>
#include <pmsis/bsp/bsp.h>


int main(void)
{
    pi_device_t *dev = PI_BSP_FLASH;

    if (pi_flash_open(dev))
    {
        printf("pi_flash_open failed\n");
        return -1;
    }

    size_t sector_size = 0, total_size = 0;
    pi_flash_get_info(dev, &sector_size, &total_size);
    printf("flash size=%u sector=%u\n",
           (unsigned)total_size, (unsigned)sector_size);

    pi_flash_close(dev);

    printf("flash_smoke OK\n");
    return 0;
}
