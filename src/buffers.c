#include "buffers.h"
#include "memory.h"
#include "main.h"
#include "disk.h"

#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/systick.h"

#include <stdbool.h>
#include <stdint.h>

// TODO: make sure that disks contain the correct number of bytes, also
// it would be nice to make sure that we are synchronised with the index pulse

void *readpointer;
uint32_t residualdata;
uint_fast8_t residualdatabytes;

uint32_t readbuffer[256]; // be careful with this one
volatile uint8_t readbufferstart;
volatile uint8_t readbufferlength;
unsigned int readpointertime; // includes residual data, so subtract 24 * currentperiod to be safe
volatile uint32_t writebuffer[256];
volatile uint8_t writebufferend;
volatile uint8_t writebufferlength;
unsigned int writebufferstarttime;

unsigned int stablereadpointertime;
uint_fast8_t stableresidualdatabytes;


bool startedonam;

// may as well be a macro
static inline void set_rawread_timers(uint_fast8_t rawbytecount, uint32_t currenttime) {
  bpassert(!(pwm_hw->en & 0b01001110));
  // TODO: check behaviour when the readpointertime is little above the currenttime
  int32_t timedifference = readpointertime - currenttime;
  if (timedifference < 0) {
    // either we have fallen behind or we have wrapped---let's assume wrapped
    timedifference += ROTATION_PERIOD;
  }
  if (lastwordbytecount) {
    // don't need to do this if our last byte is full
    if (timedifference > (4*8 << currentshift) - PWM_ERROR_MARGIN) {
      // TODO: fix this
      if (timedifference + PWM_ERROR_MARGIN > ((4 + lastwordbytecount)*8 << currentshift)) {
	pwm_set_counter(1, 65536 - (timedifference
				    - ((4 + lastwordbytecount)*8 << currentshift)
				    + PWM_ERROR_MARGIN));
      } else {
	DI();
	if (pio_sm_get_tx_fifo_level(pio0, 0) != 1 || readbufferlength) {
	  oops();
	}
	// change the amount to output
	pio0_hw->sm[0].shiftctrl |=
	  (8 * lastwordbytecount) << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB;
	status.rawreadstage++;
	if (status.rawreadstage != EXHAUSTED_FM_AM &&
	    status.rawreadstage != EXHAUSTED_MFM_AM) {
	  oops();
	}
	EI();
      }
      pwm_set_counter(6, 65536 - (timedifference
				  - (4*8 << currentshift)
				  + PWM_ERROR_MARGIN));
    } else {
      // angery
      oops();
    }
  }
  pwm_set_counter(2, 65536 - (timedifference
			      + PWM_ERROR_MARGIN));
  pwm_set_counter(3, 65536 - (timedifference
			      + (8*rawbytecount << currentshift)
			      + PWM_ERROR_MARGIN));
  // enable timers
  DI();
  if (status.reading) {
    if (lastwordbytecount) {
      pwm_set_irq_mask_enabled(0b1001110, true);
      pwm_hw->en |= 0b01001110;
    } else {
      pwm_set_irq_mask_enabled(0b0001100, true);
      pwm_hw->en |= 0b00001100;
    }
  }
  EI();
}


// should probably be a macro
// doesn't work on low values of endtime?
static inline uint32_t byte_offset(uint32_t endtime, uint32_t targettime, uint32_t length) {
  uint32_t starttime = endtime - (length*8 << currentshift);
  return (targettime - starttime) / (8 << currentshift);
}

static inline uint32_t get_currenttime(void) {
  uint_fast8_t mytimebasenumber = timebasenumber;
  uint16_t mycounter = pwm_get_counter(0);
  // make sure we didn't read at an inopportune time
  if (mytimebasenumber != timebasenumber) {
    mytimebasenumber = timebasenumber;
    mycounter = pwm_get_counter(0);
  }
  return (mytimebasenumber << 16) + mycounter;
}

// jumps the readbuffer using the TOC
static bool jump_readbuffer(void) {
  DI();
  if (!selecteddrive) {
    return false;
  }
  struct toc *toc = selecteddrive->tracks[selecteddrive->currenttrack];
  EI();
  uint32_t targettime = get_currenttime() + ((8 << currentshift) + PWM_ERROR_MARGIN);
  targettime = (targettime / (8 << currentshift)); // the TOC times are in bytes
  uint_fast8_t middle = 0;
  if (targettime < toc->times[0]) {
    readpointer = toc->cdr;
    readpointertime = 0;
    return true;
  }
  // somehow radix is easier for me to understand than the normal way of doing binary search
  for (int i = 3; i; i--) {
    if (targettime > toc->times[middle | (1 << i)]) {
      middle |= (1 << i);
    }
  }
  readpointer = (toc->addresses[middle] << 3) + (void *)trackstorage;
  readpointertime = (8 << currentshift) * toc->times[middle];
  return true;
}

// TODO: check other times residualdatabytes are used are correct
static inline void add_data_to_readbuffer(uint8_t *myreadbufferend,
					  uint8_t *data,
					  uint32_t end,
					  uint32_t start) {
  if (end - start < (4 - residualdatabytes)) {
    for (uint32_t i = start; i < end; i++) {
      residualdatabytes++;
      residualdata |= (data[i]) << ((4 - residualdatabytes) * 8);
    }
  } else if (end - start == (4 - residualdatabytes)) {
    for (uint32_t i = start; i < end; i++) {
      residualdatabytes++;
      residualdata |= (data[i]) << ((4 - residualdatabytes) * 8);
    }
    readbuffer[*myreadbufferend] = residualdata;
    residualdata = 0;
    residualdatabytes = 0;
    (*myreadbufferend)++;
  } else {
    uint_fast8_t alignedstart = start + (4 - residualdatabytes);
    for (uint32_t i = start; i < alignedstart; i++) {
      residualdatabytes++;
      residualdata |= (data[i]) << ((4 - residualdatabytes) * 8);
    }
    readbuffer[*myreadbufferend] = residualdata;
    (*myreadbufferend)++;
    residualdata = 0;
    residualdatabytes = 0;
    for (uint32_t i = alignedstart;
	 i < end - ((end - alignedstart) % 4);
	 i += 4) {
      readbuffer[*myreadbufferend] =
	(data[i] << 24) |
	(data[i + 1] << 16) |
	(data[i + 2] << 8) |
	data[i + 3];
      (*myreadbufferend)++;
    }
    for (uint32_t i = end - ((end - alignedstart) % 4);
	 i < end;
	 i++) {
      residualdatabytes++;
      residualdata |= (data[i]) << ((4 - residualdatabytes) * 8);
    }
  }
}

// only does a linear seek (fast-forward): use the TOC first
// loads the first element into the readbuffer
// assumes residualdata and readbuffer are empty
// also assumes read buffer won't change
// TODO: handle the wrapping of the time correctly - i think i have done this?
static void seek_readbuffer(void) {
  uint32_t currenttime;
  uint32_t targettime;
  uint8_t myreadbufferend = readbufferstart + readbufferlength;
  startedonam = false;
 readstart:
  bpassert(!status.rawreadstage);
  bpassert(!(pwm_hw->en & 0b01011110));
  currenttime = get_currenttime();
  // we aim for the first byte to contain targettime
  targettime = currenttime + ((8 << currentshift) + TARGET_ERROR_MARGIN);
  if (readpointertime > targettime) {
    // we have overshot (unlikely) or the time has wrapped (more likely)
    if (readpointertime > ROTATION_PERIOD + ((8 << currentshift) + TARGET_ERROR_MARGIN)) {
      readpointertime -= ROTATION_PERIOD;
    } else {
      jump_readbuffer();
    }
  }
  bpassert(readbufferlength == 0);
  bpassert(residualdatabytes == 0);
  switch (((struct bytes *)readpointer)->type) {
  case TOC:
    // start of the disc
    readpointer = ((struct toc *)readpointer)->cdr;
    // we should have gone around one time (or none)
    bpassert((readpointertime == 0) || (readpointertime == ROTATION_PERIOD));
    readpointertime = 0;
    goto readstart;
  case DATABLOCK:
    readpointertime += 67 * (8 << currentshift);
    if (readpointertime > targettime) {
      add_data_to_readbuffer(&myreadbufferend,
			     ((struct datablock *)readpointer)->data,
			     67,
			     byte_offset(readpointertime, targettime, 67));
      readbufferlength = myreadbufferend - readbufferstart;
      readpointer = ((struct datablock *)readpointer)->cdr;
      break;
    } else {
      readpointer = ((struct datablock *)readpointer)->cdr;
      goto readstart;
    }
  case SMALLDATABLOCK:
    readpointertime += 19 * (8 << currentshift);
    if (readpointertime > targettime) {
      add_data_to_readbuffer(&myreadbufferend,
			     ((struct datablock *)readpointer)->data,
			     19,
			     byte_offset(readpointertime, targettime, 19));
      readbufferlength = myreadbufferend - readbufferstart;
      readpointer = ((struct datablock *)readpointer)->cdr;
      break;
    } else {
      readpointer = ((struct datablock *)readpointer)->cdr;
      goto readstart;
    }
  case TINYDATABLOCK:
    readpointertime += 3 * (8 << currentshift);
    if (readpointertime > targettime) {
      add_data_to_readbuffer(&myreadbufferend,
			     ((struct tinydatablock *)readpointer)->data,
			     3,
			     byte_offset(readpointertime, targettime, 3));
      readbufferlength = myreadbufferend - readbufferstart;
      readpointer = ((struct datablock *)readpointer)->cdr;
      break;
    } else {
      readpointer = ((struct datablock *)readpointer)->cdr;
      goto readstart;
    }
  case BYTES:
    readpointertime += ((struct bytes *)readpointer)->count * (8 << currentshift);
    if (readpointertime > targettime) {
      {
	int i =
	  ((struct bytes *)readpointer)->count
	  - byte_offset(readpointertime, targettime, ((struct bytes *)readpointer)->count);
	uint32_t word =
	  ((struct bytes *)readpointer)->byte
	  | (((struct bytes *)readpointer)->byte << 8)
	  | (((struct bytes *)readpointer)->byte << 16)
	  | (((struct bytes *)readpointer)->byte << 24);
	// assumption: nothing in  residualdata
	while (i >= 4) {
	  readbuffer[myreadbufferend] = word;
	  myreadbufferend++;
	  i -= 4;
	}
	residualdata = word << (8 * (4 - i));
	residualdatabytes = i;
      }
      readbufferlength = myreadbufferend - readbufferstart;
      readpointer = ((struct datablock *)readpointer)->cdr;
      break;
    } else {
      readpointer = ((struct datablock *)readpointer)->cdr;
      goto readstart;
    }
  case FMAM:
    readpointertime += (8 << currentshift);
    if (readpointertime > targettime) {
      // no residual
      lastwordbytecount = 0;
      // set up the pio
      bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
      pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
      // set up pwm interrupts
      // interrupt 4 will do whatever interrupt 2 normally would
      pwm_set_counter(3, 65536 - (readpointertime - currenttime + PWM_ERROR_MARGIN));
      DI();
      if (status.reading) {
	pwm_set_irq_enabled(3, true);
	pwm_set_enabled(3, true);
      }
      EI();
      // set status
      status.rawreadstage = EXHAUSTED_FM_AM;
      startedonam = true;
      readpointer = ((struct datablock *)readpointer)->cdr;
      while (((struct bytes *)readpointer)->type == FMAM) {
	// TODO: something when we try to add too much to the fifo
	bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
	// readpointertime will keep increasing as we add more stuff
	pwm_set_counter(3, 65536 - (readpointertime - currenttime + (8 << currentshift) + PWM_ERROR_MARGIN));
	readpointertime += (8 << currentshift);
	readpointer = ((struct datablock *)readpointer)->cdr;
      }
      if (((struct bytes *)readpointer)->type == MFMAM) {
	// TODO: something when we try to add too much to the fifo
	for (int i = 0; i < 3; i++) {
	  bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	  pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
	}
	// readpointertime will keep increasing as we add more stuff
	pwm_set_counter(3, 65536 - (readpointertime - currenttime + (24 << currentshift) + PWM_ERROR_MARGIN));
	// this gives us one residual byte
	residualdata = ((struct am *)readpointer)->byte3 << 24;
	residualdatabytes = 1;
	readpointertime += (32 << currentshift);
	// don't forget we're now doing an MFM read (if that's any different)
	status.rawreadstage = EXHAUSTED_MFM_AM;
	readpointer = ((struct datablock *)readpointer)->cdr;
      }
      break;
    } else {
      readpointer = ((struct datablock *)readpointer)->cdr;
      goto readstart;
    }
  case MFMAM:
    readpointertime += (32 << currentshift);
    if (readpointertime > targettime) {
      // no residual
      lastwordbytecount = 0;
      if (byte_offset(readpointertime, targettime, 4) < 3) {
	// set up the pio
	for (int i = 3; i; i--) {
	  bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	  pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
	}
	// set up pwm interrupts
	DI();
	if (status.reading) {
	  pwm_set_counter(3, 65536 - (readpointertime - currenttime - (8 << currentshift) + PWM_ERROR_MARGIN));
	  pwm_set_irq_enabled(3, true);
	  pwm_set_enabled(3, true);
	}
	EI();
	// set status
	status.rawreadstage = EXHAUSTED_MFM_AM;
	startedonam = true;
      }
      // this gives us one residual byte
      residualdata = ((struct am *)readpointer)->byte3 << 24;
      residualdatabytes = 1;
      readpointer = ((struct datablock *)readpointer)->cdr;
      break;
    } else {
      readpointer = ((struct datablock *)readpointer)->cdr;
      goto readstart;
    }
  }
  // round targettime down to a byte
  targettime = targettime - (targettime % ((8 << currentshift)));
  // assert that we have the correct number of bytes
  if (!startedonam) {
    bpassert(readpointertime == targettime + (4*readbufferlength + residualdatabytes)*(8 << currentshift));
  }
  if (targettime >= ROTATION_PERIOD) {
    targettime -= ROTATION_PERIOD;
  }
  // cause pwm 4 to interrupt when we need to start the SMs
  // we can't disable interrupts yet because getcurrenttime relies on them working
  currenttime = get_currenttime();
  DI();
  bpassert(!(pwm_hw->en & 0b01010110));
  if (currenttime + 2 >= targettime) {
    // oh no
    // actually no give up
    bpassert(false);
    // let's just ignore the problem
    targettime = currenttime + 1;
  }
  pwm_set_counter(4, 65536 - (targettime - currenttime));
  if (status.reading) {
    pwm_set_irq_enabled(4, true);
    pwm_set_enabled(4, true);
  }
  // update the "stable" values
  stablereadpointertime = readpointertime;
  stableresidualdatabytes = residualdatabytes;
  if (!status.rawreadstage && status.reading && readbufferlength) {
    pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
  }
  EI();
}

static inline void flush_residual(uint8_t *myreadbufferend, uint8_t *myreadbufferlength) {
  if (residualdatabytes) {
    DI();
    readbuffer[*myreadbufferend] =
      (residualdata >> (4 - residualdatabytes)) |
      (readbuffer[*myreadbufferend - 1] << residualdatabytes);
    EI();
    (*myreadbufferend)++;
    (*myreadbufferlength)++;
    lastwordbytecount = residualdatabytes;
    residualdata = 0;
    residualdatabytes = 0;
  } else {
    lastwordbytecount = 0;
  }
}

void maintain_readbuffer(void) {
  uint8_t myreadbufferstart;
  uint8_t myreadbufferlength;
  uint8_t myreadbufferend;
  DI();
  myreadbufferlength = readbufferlength;
  myreadbufferstart = readbufferstart;
  EI();
  myreadbufferend = myreadbufferlength + myreadbufferstart;
  goto firsttime;
 readstart:
  readpointer = ((struct datablock *)readpointer)->cdr;
  if (deferredtasks.urgent) {
    goto finish;
  }
  // if the read buffer is low then we want to refill it immediately,
  // and make sure that the interrupt is enabled
  if (readbufferlength < 8) {
    DI();
    // update readbuffer length
    readbufferlength = myreadbufferend - readbufferstart;
    if ((status.rawreadstage != EXHAUSTED_FM_AM) && (status.rawreadstage != EXHAUSTED_MFM_AM) && status.reading) {
      pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
    }
    // update the "stable" values
    stablereadpointertime = readpointertime;
    stableresidualdatabytes = residualdatabytes;
    EI();
      
  }
 firsttime:
  switch (((struct bytes *)readpointer)->type) {
  case TOC:
    // start of the disc
    readpointer = ((struct toc *)readpointer)->cdr;
    // we should have gone around one time (or none)
    bpassert((readpointertime == 0) || (readpointertime == ROTATION_PERIOD));
    readpointertime = 0;
    // we go to firsttime as we didn't add anything to the fifo
    goto firsttime;
  case DATABLOCK:
    if (myreadbufferlength < (255 - 17)) {
      add_data_to_readbuffer(&myreadbufferend,
			     ((struct datablock *)readpointer)->data,
			     67,
			     0);
      myreadbufferlength = myreadbufferend - myreadbufferstart;
      readpointertime += 67 * (8 << currentshift);
      goto readstart;
    } else {
      break;
    }
  case SMALLDATABLOCK:
    if (myreadbufferlength < (255 - 5)) {
      add_data_to_readbuffer(&myreadbufferend,
			     ((struct smalldatablock *)readpointer)->data,
			     19,
			     0);
      myreadbufferlength = myreadbufferend - myreadbufferstart;
      readpointertime += 19 * (8 << currentshift);
      goto readstart;
    } else {
      break;
    }
  case TINYDATABLOCK:
    if (myreadbufferlength < (255 - 1)) {
      add_data_to_readbuffer(&myreadbufferend,
			     ((struct tinydatablock *)readpointer)->data,
			     3,
			     0);
      myreadbufferlength = myreadbufferend - myreadbufferstart;
      readpointertime += 3 * (8 << currentshift);
      goto readstart;
    } else {
      break;
    }
  case BYTES:
    if (myreadbufferlength < (255 - ((((struct bytes *)readpointer)->count + 3) / 4))) {
      {
	int i = ((struct bytes *)readpointer)->count;
	uint32_t word =
	  ((struct bytes *)readpointer)->byte
	  | (((struct bytes *)readpointer)->byte << 8)
	  | (((struct bytes *)readpointer)->byte << 16)
	  | (((struct bytes *)readpointer)->byte << 24);
	// assumption: count >= 3
	if (residualdatabytes) {
	  residualdata |= word >> (8 * residualdatabytes);
	  readbuffer[myreadbufferend] = residualdata;
	  myreadbufferend++;
	  i -= (4 - residualdatabytes);
	}
	while (i >= 4) {
	  readbuffer[myreadbufferend] = word;
	  myreadbufferend++;
	  i -= 4;
	}
	residualdata = word << (8 * (4 - i));
	residualdatabytes = i;
      }
      myreadbufferlength = myreadbufferend - myreadbufferstart;
      readpointertime += ((struct bytes *)readpointer)->count * (8 << currentshift);
      goto readstart;
    } else {
      break;
    }
  case FMAM:
    if ((myreadbufferlength < 255) && !(status.rawreadstage)) {
      // flush the residual
      flush_residual(&myreadbufferend, &myreadbufferlength);
      // set up the pio
      bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
      pio_sm_put(pio0, 1, ((uint32_t)((struct am *)readpointer)->rawbyte << 16));
      // set up pwm interrupts
      uint32_t currenttime = get_currenttime();
      set_rawread_timers(1, currenttime);
      // set status
      status.rawreadstage = WAITING_FM_AM;
      readpointertime += (8 << currentshift);
      readpointer = ((struct datablock *)readpointer)->cdr;
      while (((struct bytes *)readpointer)->type == FMAM) {
	// TODO: something when we try to add too much to the fifo
	bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
	// readpointertime will keep increasing as we add more stuff
	pwm_set_counter(3, 65536 - (readpointertime - currenttime + (8 << currentshift) + PWM_ERROR_MARGIN));
	readpointertime += (8 << currentshift);
	readpointer = ((struct datablock *)readpointer)->cdr;
      }
      if (((struct bytes *)readpointer)->type == MFMAM) {
	// TODO: something when we try to add too much to the fifo
	for (int i = 3; i; i--) {
	  bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	  pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
	}
	// readpointertime will keep increasing as we add more stuff
	pwm_set_counter(3, 65536 - (readpointertime - currenttime + (24 << currentshift) + PWM_ERROR_MARGIN));
	// this gives us one residual byte
	residualdata = ((struct am *)readpointer)->byte3 << 24;
	residualdatabytes = 1;
	readpointertime += (32 << currentshift);
	// don't forget we're now doing an MFM read (if that's any different)
	//status.rawreadstage += (WAITING_MFM_AM - WAITING_FM_AM);
	readpointer = ((struct datablock *)readpointer)->cdr;
      }
      break;
    } else {
      break;
    }
  case MFMAM:
    if ((myreadbufferlength < 255) && !(status.rawreadstage)) {
      // flush the residual
      flush_residual(&myreadbufferend, &myreadbufferlength);
      // set up the pio
      for (int i = 3; i; i--) {
	bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	pio_sm_put(pio0, 1, (((struct am *)readpointer)->rawbyte << 16));
      }
      // set up pwm interrupts
      uint32_t currenttime = get_currenttime();
      set_rawread_timers(3, currenttime);
      // set status
      status.rawreadstage = WAITING_MFM_AM;
      // this gives us one residual byte
      residualdata = ((struct am *)readpointer)->byte3 << 24;
      residualdatabytes = 1;
      readpointertime += (32 << currentshift);
      readpointer = ((struct datablock *)readpointer)->cdr;
      break;
    } else {
      break;
    }
  default:
    asm volatile ("bkpt 0x01");
  }
 finish:
  DI();
  // update readbuffer length
  readbufferlength = myreadbufferend - readbufferstart;
  // we have broken so we are obviously done, and don't need to keep trying to write
  // until stuff is taken out of the readbuffer
  //deferredtasks.readmore = false;
  // update the "stable" values
  stablereadpointertime = readpointertime;
  stableresidualdatabytes = residualdatabytes;
  EI();
}

void stop_read(void) {
  // restart and disable state machines
  pio0_hw->ctrl = 0b11110000;
  pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, false);
  pio_sm_clear_fifos(pio0, 0);
  pio_sm_clear_fifos(pio0, 1);
  // disable these pwm interrupts
  pwm_set_irq_mask_enabled(0b1011110, false);
  pwm_hw->en &= ~0b01011110;
  status.reading = false;
  deferredtasks.startread = false;
  status.rawreadstage = NO_RAW_READ;
  pio0_hw->sm[0].shiftctrl &=
    ~(0b11111 << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB);
  bpassert(!(pio0_hw->fdebug & 0x01000000));
}
  

static void reset_read(void) {
  residualdatabytes = 0;
  residualdata = 0;
  readbufferlength = 0;
  status.rawreadstage = NO_RAW_READ;
  bpassert(!(pio0_hw->ctrl & 0b1111));
  pio_sm_clear_fifos(pio0, 0);
  pio_sm_clear_fifos(pio0, 1);
  bpassert(!(pio0_hw->fdebug & 0x01000000));
  bpassert(!(pwm_hw->en & 0b01011110));
}

void maintain_buffers(void) {
  DI();
  deferredtasks.urgent = false;
  if (pwm_hw->en & 0b00000100) {
    bpassert(!((status.rawreadstage == ONGOING_FM_AM) ||
	       (status.rawreadstage == ONGOING_MFM_AM)));
  }
  if (selecteddrive) {
    bpassert(selecteddrive->selected);
  }
  EI();
  // stop
  DI();
  if (deferredtasks.stop) {
    status.reading = false;
    deferredtasks.stop = false;
    bpassert(!(deferredtasks.startread || deferredtasks.changetrack));
    EI();
  } else {
    EI();
  }
  // read
  DI();
  if (deferredtasks.startread) {
    bpassert(!deferredtasks.stop);
    reset_read();
    bpassert(selecteddrive);
    bpassert(selecteddrive->selected);
    status.reading = true;
    deferredtasks.readmore = true;
    deferredtasks.startread = false;
    EI();
    // seek
    if (jump_readbuffer()) {
      seek_readbuffer();
    }
  } else {
    EI();
  }
  // change track - basically the same as stopping and starting again
  DI();
  if (deferredtasks.changetrack) {
    bpassert(!deferredtasks.stop);
    deferredtasks.changetrack = false;
    reset_read();
    status.reading = true;
    EI();
    if (jump_readbuffer()) {
      seek_readbuffer();
    }
  } else {
    EI();
  }
  if (status.reading &&
      deferredtasks.readmore &&
      (status.rawreadstage != WAITING_FM_AM) &&
      (status.rawreadstage != WAITING_MFM_AM)) {
    maintain_readbuffer();
    // just to be safe
    DI();
    if ((status.rawreadstage != EXHAUSTED_FM_AM) && (status.rawreadstage != EXHAUSTED_MFM_AM) && status.reading) {
      pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
    }
    EI();
  }
  DI();
  if (pwm_hw->en & 0b00000100) {
    bpassert(!((status.rawreadstage == ONGOING_FM_AM) ||
	       (status.rawreadstage == ONGOING_MFM_AM)));
  }
  if (selecteddrive) {
    bpassert(selecteddrive->selected);
  }
  EI();
}
