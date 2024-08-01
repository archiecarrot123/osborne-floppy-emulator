#pragma once

#include <stdint.h>


#define PWM_ERROR_MARGIN 4

#define BUFFERS_IRQ_NUMBER 31


extern uint32_t readbuffer[256]; // be careful with this one
extern volatile uint8_t readbufferstart;
extern volatile uint8_t readbufferlength;
extern unsigned int readpointertime; // includes residual data, so subtract 24 * currentperiod to be safe
extern volatile uint32_t writebuffer[256];
extern volatile uint8_t writebufferend;
extern volatile uint8_t writebufferlength;
extern unsigned int writebufferstarttime;

extern uint_fast8_t residualdatabytes;

void maintain_buffers(void);
