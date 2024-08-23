#include <stdio.h>
#include "pico/stdlib.h"
#include "tusb.h"
#include <string.h>
#include <stdint.h>
#include "ff.h"
#include "diskio.h"

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

int main(void) {
  msc_fat_init();
  // char *s = "hello_world\n";
  // send_message(s);
  // send_message("skibidi toilet\n");
}

