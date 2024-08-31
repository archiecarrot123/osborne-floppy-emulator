#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

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
