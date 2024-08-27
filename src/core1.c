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
    if (request.diskid == DEFAULT_DISK_ID) {
      bpassert(request.sectorcount <= 10);
      for (int i = 0; i < request.sectorcount; i++) {
	multicore_fifo_push_blocking((uint32_t)&(defaultdisk[10*request.trackno + i]));
      }
    } else {
      bpassert(false);
    }
  }
}
