#include "core1.h"
#include "disk.h"
#include "main.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

// should be at a fixed address in flash
struct sector *defaultdisk = (struct sector *)0x10100000;

void core1_entry(void) {
  union trackrequest request;
  while (true) {
    request.asword = multicore_fifo_pop_blocking();
    if (request.diskid >= DEFAULT_DISK_ID &&
	(request.diskid < DEFAULT_DISK_ID + FLASH_DISK_COUNT)) {
      bpassert(request.sectorcount == DEFAULT_DISK_SECTORS_PER_TRACK);
      for (int i = 0; i < request.sectorcount; i++) {
	multicore_fifo_push_blocking((uint32_t)((void *)defaultdisk
						+ (((request.trackno
						     * DEFAULT_DISK_SECTORS_PER_TRACK) + i)
						   * ((128 << DEFAULT_DISK_SECTOR_LENGTH) + 4))
						+ 0x00020000 * (request.diskid - DEFAULT_DISK_ID)));
      }
    } else {
      bpassert(false);
    }
  }
}
