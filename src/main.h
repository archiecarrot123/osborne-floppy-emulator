#pragma once

#include "hardware/pio.h"
#include "hardware/structs/scb.h"

#include <stdbool.h>
#include <stdint.h>

// we're going to use the PWM to generate interrupts for the sectors
#define TIMEBASE_DIVIDER 192
#define TIMEBASE_FREQUENCY (SYS_CLK_KHZ / TIMEBASE_DIVIDER) // 500 KHz
#define FM_PERIOD (TIMEBASE_FREQUENCY / 125) // 4 ticks
#define MFM_PERIOD (TIMEBASE_FREQUENCY / 250) // 2 ticks
#define ROTATION_TIME 200 // 200 ms
#define ROTATION_PERIOD (ROTATION_TIME * TIMEBASE_FREQUENCY) // 100 000
#define TIMEBASE_PER_ROTATION ((ROTATION_PERIOD + 65535) / 65536) // 2
#define TIMEBASE_REMAINDER (ROTATION_PERIOD % 65536) // 34 464

#define INDEX_PULSE_TIME 4000 // 4 ms
#define INDEX_PULSE_LENGTH ((INDEX_PULSE_TIME * TIMEBASE_FREQUENCY) / 1000) // 2000

#define FM_SHIFT 2
#if (1 << FM_SHIFT) != FM_PERIOD
#error "FM_SHIFT wrong value"
#endif
#define MFM_SHIFT 1
#if (1 << MFM_SHIFT) != MFM_PERIOD
#error "MFM_SHIFT wrong value"
#endif

#define DI() asm volatile ("CPSID i")
#define EI() asm volatile ("CPSIE i")
//#define DI()
//#define EI()

#define BREAK_ON_ERROR 1
#define BREAK_ON_OOPS 1

#if BREAK_ON_ERROR
#define bpassert(condition) if (!(condition)) asm volatile ("bkpt 0x03")
#else
// cause a complete reset
#define bpassert(condition) if (!(condition)) scb_hw->aircr = 0x05FA0004
#endif

// use this when encountering timing issues
// it will recover faster than bpassert as it only causes
// reading to restart, rather than causing a full reset
void oops(void);

struct deferredtasks {
  bool urgent      : 1;
  bool changedisk  : 1;
  bool startread   : 1;
  bool startwrite  : 1;
  bool stop        : 1;
  bool changetrack : 1;
  bool readmore    : 1;
  bool writemore   : 1;
};

// waiting:   don't add to the readbuffer or the rawread fifo
// exhausted: don't put data in either fifo
// ongoing:   don't put data in rawread fifo
enum rawreadstage {
  NO_RAW_READ      = 0,
  WAITING_FM_AM    = 5,
  EXHAUSTED_FM_AM  = 6,
  ONGOING_FM_AM    = 7,
  WAITING_MFM_AM   = 9,
  EXHAUSTED_MFM_AM = 10,
  ONGOING_MFM_AM   = 11
};

struct status {
  bool reading  : 1;
  bool writing  : 1;
  bool mfm      : 1;
  bool selected : 1;
  enum rawreadstage rawreadstage : 4;
};

struct piooffsets {
  unsigned int rawwrite;
  unsigned int read;
  unsigned int rawread;
};

struct pioconfigs {
  pio_sm_config rawwrite;
  pio_sm_config fmread;
  pio_sm_config mfmread;
  pio_sm_config fmrawread;
  pio_sm_config mfmrawread;
};


extern volatile uint_fast8_t timebasenumber;

extern volatile struct deferredtasks deferredtasks;
extern volatile struct status status;

extern uint_fast8_t currentperiod;
extern uint_fast8_t currentshift;

// used for rawread so the data doesn't have to be word-aligned
extern uint_fast8_t lastwordbytecount;

extern struct piooffsets piooffsets;
extern struct pioconfigs pioconfigs;
