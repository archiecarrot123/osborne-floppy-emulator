/**
 * @file Pico-USB-Host-MIDI-Adapter.c
 * @brief A USB Host to Serial Port MIDI adapter that runs on a Raspberry Pi
 * Pico board
 * 
 * MIT License

 * Copyright (c) 2022 rppicomidi

 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:

 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"

#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pio_usb.h"
#include "pio_usb_configuration.h"

#include "tusb.h"
#include "class/msc/msc_host.h"
#include "ff.h"
#include "diskio.h"

#include "semihosting.c"


static scsi_inquiry_resp_t inquiry_resp;
static FATFS fatfs[CFG_TUH_DEVICE_MAX];
// static_assert(FF_VOLUMES == CFG_TUH_DEVICE_MAX);


void main_loop_task()
{
  tuh_task();
}

// core1: handle host events
static volatile bool core1_booting = true;
static volatile bool core0_booting = true;
void core1_main() {
    sleep_ms(10);
    // Use tuh_configure() to pass pio configuration to the host stack
    // Note: tuh_configure() must be called before
    pio_usb_configuration_t pio_cfg = {27,1,PIO_SM_USB_TX_DEFAULT, PIO_USB_DMA_TX_DEFAULT, 1, PIO_SM_USB_RX_DEFAULT, PIO_SM_USB_EOP_DEFAULT, NULL, PIO_USB_DEBUG_PIN_NONE, PIO_USB_DEBUG_PIN_NONE, false, PIO_USB_PINOUT_DMDP };
    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
    // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
    // port1) on core1
    tuh_init(1);
    core1_booting = false;
    while(core0_booting) {
    }
    while (true) {
        tuh_task(); // tinyusb host task
    }
}

int main()
{
    ocd_sendline("my bad code is running\r\n");
    sleep_ms(10);

    stdio_init_all();
    // all USB Host task run in core1
    multicore_reset_core1();
    multicore_launch_core1(core1_main);
    ocd_sendline("Pico USB Host Mass Storage Class Demo\r\n");

    msc_fat_init();
    core0_booting = false;
    while (1) {
        main_loop_task();
    }
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// MSC implementation
//--------------------------------------------------------------------+
bool inquiry_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data)
{
    if (cb_data->csw->status != 0) {
        ocd_sendline("Inquiry failed\r\n");
        return false;
    }

    // Print out Vendor ID, Product ID and Rev
    ocd_sendline("%.8s %.16s rev %.4s\r\n", inquiry_resp.vendor_id, inquiry_resp.product_id, inquiry_resp.product_rev);

    // Get capacity of device
    uint32_t const block_count = tuh_msc_get_block_count(dev_addr, cb_data->cbw->lun);
    uint32_t const block_size = tuh_msc_get_block_size(dev_addr, cb_data->cbw->lun);

    ocd_sendline("Disk Size: %lu MB\r\n", block_count / ((1024*1024)/block_size));
    ocd_sendline("Block Count = %lu, Block Size: %lu\r\n", block_count, block_size);

    return true;
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    uint8_t pdrv = msc_map_next_pdrv(dev_addr);

    assert(pdrv < FF_VOLUMES);
    msc_fat_plug_in(pdrv);
    uint8_t const lun = 0;
    tuh_msc_inquiry(dev_addr, lun, &inquiry_resp, inquiry_complete_cb, 0);
    char path[3] = "0:";
    path[0] += pdrv;
    if ( f_mount(&fatfs[pdrv],path, 0) != FR_OK ) {
        ocd_sendline("mount drive %s failed\r\n", path);
        return;
    }
    if (f_chdrive(path) != FR_OK) {
        ocd_sendline("f_chdrive(%s) failed\r\n", path);
        return;
    }
    ocd_sendline("\r\nMass Storage drive %u is mounted\r\n", pdrv);
    ocd_sendline("Run the set-date and set-time commands so file timestamps are correct\r\n\r\n");
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    uint8_t pdrv = msc_unmap_pdrv(dev_addr);
    char path[3] = "0:";
    path[0] += pdrv;

    f_mount(NULL, path, 0); // unmount disk
    msc_fat_unplug(pdrv);
    ocd_sendline("Mass Storage drive %u is unmounted\r\n", pdrv);
}

