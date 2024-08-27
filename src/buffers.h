#pragma once

#include <stdint.h>
#include <stdbool.h>


#define PWM_ERROR_MARGIN 4
#define TARGET_ERROR_MARGIN 32

#define BUFFERS_IRQ_NUMBER 31


extern uint32_t readbuffer[256]; // be careful with this one
extern volatile uint8_t readbufferstart;
extern volatile uint8_t readbufferlength;
extern unsigned int readpointertime;
extern unsigned int stablereadpointertime; // includes residual data, so subtract 24 * currentperiod to be safe
extern volatile uint32_t writebuffer[256];
extern volatile uint8_t writebufferend;
extern volatile uint8_t writebufferlength;
extern unsigned int writebufferstarttime;

extern uint_fast8_t stableresidualdatabytes;

extern bool startedonam;

void maintain_buffers(void);

void stop_read(void);
