/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
 // a lot of code (but not all) is borrowed from tinyusb examples msc-file-explorer in pico-sdk

#include <stdio.h>
#include "pico/stdlib.h"
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "tusb.h"
#include "ff.h"
#include "diskio.h"


//------------- Elm Chan FatFS -------------//
static FATFS fatfs[CFG_TUH_DEVICE_MAX]; // for simplicity only support 1 LUN per device
static volatile bool _disk_busy[CFG_TUH_DEVICE_MAX];

static scsi_inquiry_resp_t inquiry_resp;


void send_command(int command, void *message) {
   asm("mov r0, %[cmd];"
       "mov r1, %[msg];"
       "bkpt #0xAB"
         :
         : [cmd] "r" (command), [msg] "r" (message)
         : "r0", "r1", "memory");
}

void send_message(char *s){
  uint32_t m[3] = { 2/*stderr*/, (uint32_t)s, strlen(s) };
  send_command(0x05/* some interrupt ID */, m);

}

void ocd_sendline(char* fmt_str, ...) {
  va_list argptr;
  va_start(argptr, fmt_str);
  char buffer[120]; // arbitary number of characters, 120 seemed fine
  vsprintf(buffer, fmt_str, argptr);
  va_end(argptr);
  send_message(buffer);
}

bool msc_app_init() {
  for(int i=0; i<CFG_TUH_DEVICE_MAX; i++) {
    _disk_busy[i] = false;
  }
  return true;
}

// bool msc_app_task() {
// }

bool inquiry_complete_cb(uint8_t dev_addr, tuh_msc_complete_data_t const * cb_data)
{
  msc_cbw_t const* cbw = cb_data->cbw;
  msc_csw_t const* csw = cb_data->csw;

  if (csw->status != 0)
  {
    ocd_sendline("Inquiry failed\r\n");
    return false;
  }

  // Print out Vendor ID, Product ID and Rev
  ocd_sendline("%.8s %.16s rev %.4s\r\n", inquiry_resp.vendor_id, inquiry_resp.product_id, inquiry_resp.product_rev);

  // Get capacity of device
  uint32_t const block_count = tuh_msc_get_block_count(dev_addr, cbw->lun);
  uint32_t const block_size = tuh_msc_get_block_size(dev_addr, cbw->lun);

  ocd_sendline("Disk Size: %lu MB\r\n", block_count / ((1024*1024)/block_size));
  ocd_sendline("Block Count = %lu, Block Size: %lu\r\n", block_count, block_size);

  // For simplicity: we only mount 1 LUN per device
  uint8_t const drive_num = dev_addr-1;
  char drive_path[3] = "0:";
  drive_path[0] += drive_num;

  if ( f_mount(&fatfs[drive_num], drive_path, 1) != FR_OK )
  {
    ocd_sendline("mount failed\n");
  }

  // change to newly mounted drive
  f_chdir(drive_path);

  // print the drive label
//  char label[34];
//  if ( FR_OK == f_getlabel(drive_path, label, NULL) )
//  {
//    puts(label);
//  }

  return true;
}

// these are called by tinyusb whenever a usb is mounted
void tuh_msc_mount_cb(uint8_t dev_addr)
{
  ocd_sendline("A MassStorage device is mounted\r\n");

  uint8_t const lun = 0;
  tuh_msc_inquiry(dev_addr, lun, &inquiry_resp, inquiry_complete_cb, 0);
}

void tuh_msc_umount_cb(uint8_t dev_addr)
{
  ocd_sendline("A MassStorage device is unmounted\r\n");
  asm volatile ("bkpt 0x00");

  uint8_t const drive_num = dev_addr-1;
  char drive_path[3] = "0:";
  drive_path[0] += drive_num;

  f_unmount(drive_path);
}


//--------------------------------------------------------------------+
// DiskIO (borrowed from tinyusb examples in pico-sdk)
//--------------------------------------------------------------------+

// disk_status, disk_initialise, disk_write, disk_ioctl, disk_read
// http://elm-chan.org/fsw/ff/00index_e.html
// LBA is Logical Block Addressing

static void wait_for_disk_io(BYTE pdrv)
// pdrv is physical drive number (id)
{
  while(_disk_busy[pdrv])
  {
    tuh_task();
  }
}

static bool disk_io_complete(uint8_t dev_addr, tuh_msc_complete_data_t const * cb_data)
{
  (void) dev_addr; (void) cb_data;
  _disk_busy[dev_addr-1] = false;
  return true;
}

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
  uint8_t dev_addr = pdrv + 1;
  return tuh_msc_mounted(dev_addr) ? 0 : STA_NODISK;
}

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
  (void) pdrv;
	return 0; // nothing to do
}

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	uint8_t const dev_addr = pdrv + 1;
	uint8_t const lun = 0;

	_disk_busy[pdrv] = true;
	tuh_msc_read10(dev_addr, lun, buff, sector, (uint16_t) count, disk_io_complete, 0);
	wait_for_disk_io(pdrv);

	return RES_OK;
}

// #if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	uint8_t const dev_addr = pdrv + 1;
	uint8_t const lun = 0;

	_disk_busy[pdrv] = true;
	tuh_msc_write10(dev_addr, lun, buff, sector, (uint16_t) count, disk_io_complete, 0);
	wait_for_disk_io(pdrv);

	return RES_OK;
}

// #endif

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
  uint8_t const dev_addr = pdrv + 1;
  uint8_t const lun = 0;
  switch ( cmd )
  {
    case CTRL_SYNC:
      // nothing to do since we do blocking
      return RES_OK;

    case GET_SECTOR_COUNT:
      *((DWORD*) buff) = (WORD) tuh_msc_get_block_count(dev_addr, lun);
      return RES_OK;

    case GET_SECTOR_SIZE:
      *((WORD*) buff) = (WORD) tuh_msc_get_block_size(dev_addr, lun);
      return RES_OK;

    case GET_BLOCK_SIZE:
      *((DWORD*) buff) = 1;    // erase block size in units of sector size
      return RES_OK;

    default:
      return RES_PARERR;
  }

	return RES_OK;
}

int main(void) {
  // char *s = "hello_world\n";
  // send_message(s);
  // send_message("skibidi toilet\n");
  send_message("running my bad code\n");
  tuh_init(BOARD_TUH_RHPORT);
  msc_app_init();

  while (1)
  {
    // tinyusb host task
    tuh_task();
    // msc_app_task();
  }

  return 0;
}
