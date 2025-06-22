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

#include "hardware/clocks.h"

#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "pio_usb.h"
#include "pio_usb_configuration.h"

#include "tusb.h"
#include "class/msc/msc_host.h"
#include "ff.h"
#include "diskio.h"

#include "ffconf.h"
#include "tusb_config.h"

#include "semihosting.c"

static scsi_inquiry_resp_t inquiry_resp;
static FATFS fatfs[CFG_TUH_DEVICE_MAX];
static_assert(FF_VOLUMES == CFG_TUH_DEVICE_MAX);

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// MSC implementation
//--------------------------------------------------------------------+
bool inquiry_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data)
{
    ocd_sendline("Inquired USB\n");
    // if (cb_data->csw->status != 0) {
    //     ocd_sendline("Inquiry failed\r\n");
    //     return false;
    // }

    // // Print out Vendor ID, Product ID and Rev
    // ocd_sendline("%.8s %.16s rev %.4s\r\n", inquiry_resp.vendor_id, inquiry_resp.product_id, inquiry_resp.product_rev);

    // // Get capacity of device
    // uint32_t const block_count = tuh_msc_get_block_count(dev_addr, cb_data->cbw->lun);
    // uint32_t const block_size = tuh_msc_get_block_size(dev_addr, cb_data->cbw->lun);

    // ocd_sendline("Disk Size: %lu MB\r\n", block_count / ((1024*1024)/block_size));
    // ocd_sendline("Block Count = %lu, Block Size: %lu\r\n", block_count, block_size);

    // return true;
}

void tuh_msc_mount_cb(uint8_t dev_addr)
{
    ocd_sendline("mounted\n");
    asm("bkpt");
    // uint8_t pdrv = msc_map_next_pdrv(dev_addr);

    // assert(pdrv < FF_VOLUMES);
    // msc_fat_plug_in(pdrv);
    // uint8_t const lun = 0;
    // tuh_msc_inquiry(dev_addr, lun, &inquiry_resp, inquiry_complete_cb, 0);
    // char path[3] = "0:";
    // path[0] += pdrv;
    // if ( f_mount(&fatfs[pdrv],path, 0) != FR_OK ) {
    //     ocd_sendline("mount drive %s failed\r\n", path);
    //     return;
    // }
    // if (f_chdrive(path) != FR_OK) {
    //     ocd_sendline("f_chdrive(%s) failed\r\n", path);
    //     return;
    // }
    // ocd_sendline("\r\nMass Storage drive %u is mounted\r\n", pdrv);
    // ocd_sendline("Run the set-date and set-time commands so file timestamps are correct\r\n\r\n");
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
    ocd_sendline("unmounted\n");
    asm("bkpt");
    // uint8_t pdrv = msc_unmap_pdrv(dev_addr);
    // char path[3] = "0:";
    // path[0] += pdrv;

    // f_mount(NULL, path, 0); // unmount disk
    // msc_fat_unplug(pdrv);
    // ocd_sendline("Mass Storage drive %u is unmounted\r\n", pdrv);
}

void main_loop_task()
{
}

uint32_t int_power(uint32_t base, uint32_t exponent) {
    uint32_t value = 1;
    for (int i=0; i<exponent; i++) {
        value *= base;
    }
    return value;
}

void set_register(uint32_t reg, uint32_t offset, uint32_t size, uint32_t value) {
    // registers aren't reset without power cycling
    // Write to 32 bit register "reg", where the "value" start location is "offset" and size is "size"
    *(uint32_t*) (reg) &= ~((int_power(2, size)-1) << offset); // Clear
    *(uint32_t*) (reg) |= (value << offset); // Set
}

void set_safe_clocks() {
    ocd_sendline("\nSetting safe clocks (clk_sys, clk_ref)\n");
    // Set CLK_REF
    set_register(CLOCKS_BASE + 0x30, 0, 2, 0x0); // Set ROSC as the source of clk_ref
    while (*((uint32_t*) (CLOCKS_BASE + 0x38)) != 0x1) {}; // Poll CLK_REF_SELECTED until glitchless mux switches to ROSC as above

    // Set CLK_SYS
    set_register(CLOCKS_BASE + 0x3c, 0, 1, 0x0); // Set CLK_REF as the source of clk_sys
    while (*((uint32_t*) (CLOCKS_BASE + 0x44)) != 0x1) {}; // Poll CLK_SYS_SELECTED until glitchless mux switches to CLK_SYS as above
}

void configure_pll() {
    ocd_sendline("Configuring PLL\n");

    // From datasheet:
    // Input clock is 4MHz. The PLL's VCO must run between 750 and 1600Mhz.
    // Output clock is (FREF/REFDIV)*FBDIV/(POSTDIV1*POSTDIV2).
    // VCO frequency is (FREF/REFDIV)*FBDIV.

    // Wait uhoh
    // • Minimum reference frequency (FREF / REFDIV) is 5MHz
    // We're using FREF = 4MHz
    // ohnoooooooo
    // gonna try it anyway ig
    // yay it works :) !!!!

    // SYS PLL (120MHz)
    set_register(PLL_SYS_BASE + 0x0, 0, 6, 1); // REFDIV = 1
    set_register(PLL_SYS_BASE + 0x8, 0, 12, 300); // FBDIV = 300
    set_register(PLL_SYS_BASE + 0xc, 16, 3, 5); // POSTDIV1 = 5
    set_register(PLL_SYS_BASE + 0xc, 12, 3, 1); // POSTDIV2 = 1

    // USB PLL (48MHz)
    set_register(PLL_USB_BASE + 0x0, 0, 6, 1); // REFDIV = 1
    set_register(PLL_USB_BASE + 0x8, 0, 12, 300); // FBDIV = 300
    set_register(PLL_USB_BASE + 0xc, 16, 3, 5); // POSTDIV1 = 5
    set_register(PLL_USB_BASE + 0xc, 12, 3, 5); // POSTDIV2 = 5
}

void set_clock_out_gpio() {
    // GPOUT0/GPCLK is on GPIO 21
    
    // GPOUT0
    ocd_sendline("Setting GPCLK0 (GPIO 21) debug output\n");
    
    // GPIO settings
    set_register(IO_BANK0_BASE + 4 + 8*21, 0, 5, 0x8); // Set GPIO21 function to GPCLK0
    set_register(IO_BANK0_BASE + 4 + 8*21, 8, 2, 0x0); // Output driven from function
    set_register(IO_BANK0_BASE + 4 + 8*21, 12, 2, 0x3); // Output enabled

    // Pad settings
    set_register(PADS_BANK0_BASE + 4 + 4*21, 2, 1, 0x0); // Pull down disable
    set_register(PADS_BANK0_BASE + 4 + 4*21, 6, 1, 0x0); // Input disable
    set_register(PADS_BANK0_BASE + 4 + 4*21, 4, 2, 0x0); // Drive 2mA
    set_register(PADS_BANK0_BASE + 4 + 4*21, 0, 1, 0x1); // Fast slewing

    // GPCLK settings
    // set_register(CLOCKS_BASE+0x00, 5, 4, 0x4); // Set output to ROSC
    // set_register(CLOCKS_BASE+0x00, 5, 4, 0x5); // Set output to XOSC
    // set_register(CLOCKS_BASE+0x00, 5, 4, 0x6); // Set output to CLK_SYS
    set_register(CLOCKS_BASE+0x00, 5, 4, 0x7); // Set output to CLK_USB
    // set_register(CLOCKS_BASE+0x00, 5, 4, 0x0); // Set output to CLKSRC_PLL_SYS
    // set_register(CLOCKS_BASE+0x00, 5, 4, 0x3); // Set output to CLKSRC_PLL_USB
    // set_register(CLOCKS_BASE+0x04, 8, 24, 10); // Divide by 10
    set_register(CLOCKS_BASE+0x04, 8, 24, 1000); // Divide by 1000
    // set_register(CLOCKS_BASE+0x04, 8, 24, 5*1000); // Divide by 5*1000
    set_register(CLOCKS_BASE+0x00, 11, 1, 0x1); // Enable output
}

void set_clocks_for_usb() {
    // Assumes set_safe_clocks has been run and CLK_REF/CLK_SYS are running off ROSC
    ocd_sendline("Setting clocks for USB\n");
    
    // CLK_SYS
    set_register(CLOCKS_BASE + 0x3c, 5, 3, 0x0); // Set AUXSRC to CLKSRC_PLL_SYS
    set_register(CLOCKS_BASE + 0x3c, 0, 1, 0x1); // Set SRC to CLKSRC_CLK_SYS_AUX
    while (*((uint32_t*) (CLOCKS_BASE + 0x44)) != 0x2) {} // Wait for SRC to switch to CLKSRC_CLK_SYS_AUX
    set_register(CLOCKS_BASE + 0x40, 8, 24, 1); // Set divider to 1

    // CLK_USB
    set_register(CLOCKS_BASE + 0x54, 11, 1, 0x0); // Disable clock 
    set_register(CLOCKS_BASE + 0x54, 5, 3, 0x0); // Set AUXSRC to CLKSRC_PLL_USB
    set_register(CLOCKS_BASE + 0x58, 8, 2, 1); // Set divider to 1
    set_register(CLOCKS_BASE + 0x54, 11, 1, 0x1); // Enable clock
}

// core1: handle host events
static volatile bool core1_booting = true;
static volatile bool core0_booting = true;
void core1_main() {
    sleep_ms(10);

    // Use tuh_configure() to pass pio configuration to the host stack
    // Note: tuh_configure() must be called before
    // 26 is d-, 27 is d+
    // pio_usb_configuration_t pio_cfg = {27,1,PIO_SM_USB_TX_DEFAULT, PIO_USB_DMA_TX_DEFAULT, 1, PIO_SM_USB_RX_DEFAULT, PIO_SM_USB_EOP_DEFAULT, NULL, PIO_USB_DEBUG_PIN_NONE, PIO_USB_DEBUG_PIN_NONE, false, PIO_USB_PINOUT_DMDP};
    // pio_usb_configuration_t pio_cfg = {26,1,PIO_SM_USB_TX_DEFAULT, PIO_USB_DMA_TX_DEFAULT, 1, PIO_SM_USB_RX_DEFAULT, PIO_SM_USB_EOP_DEFAULT, NULL, PIO_USB_DEBUG_PIN_NONE, PIO_USB_DEBUG_PIN_NONE, false, PIO_USB_PINOUT_DPDM};

    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp=27;
    // pio_cfg.pio_tx_num=1;
    // pio_cfg.pio_rx_num=1;
    pio_cfg.pinout=PIO_USB_PINOUT_DMDP;
    tuh_configure(CFG_TUH_RPI_PIO_USB, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    // To run USB SOF interrupt in core1, init host stack for pio_usb (roothub
    // port1) on core1
    tuh_init(CFG_TUH_RPI_PIO_USB); // Maybe i need to run tusb_init instead
    core1_booting = false;
    while(core0_booting) {
    }
    ocd_sendline("core1 boot done\n");
    while (true) {
        tuh_task(); // tinyusb host task
    }
}

int main()
{
    // Set clocks
    set_safe_clocks();
    configure_pll();
    set_clocks_for_usb();
    set_clock_out_gpio();

    // while (1) {
    //     sleep_ms(10);
    // }

    // asm("bkpt");

    sleep_ms(10);

    stdio_init_all(); // i think i need this

    // all USB Host task run in core1
    // multicore_reset_core1();
    // multicore_launch_core1(core1_main);
    // ocd_sendline("started core1\n");

    msc_fat_init();
    core0_booting = false;
    ocd_sendline("core0 boot done\n");

    core1_main();

    asm("bkpt");
    
    while (1) {
        main_loop_task();
    }
}

