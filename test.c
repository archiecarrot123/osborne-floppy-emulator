#include <stdbool.h>

#define XOSC_KHZ _u(4000)

#include "hardware/gpio.h"

int main(void) {
  // set function to SIO
  gpio_init_mask(0x3FFFFFFF);

  gpio_set_dir_out_masked(0x3FFFFFFF); // every pin
  
  while(1) {
    // hammer gpios
    gpio_xor_mask(0x3FFFFFFF);
  }
}
