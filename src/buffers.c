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

// may as well be a macro
static inline void set_rawread_timers(uint_fast8_t rawbytecount, uint32_t currenttime) {
  // TODO: check behaviour when the readpointertime is little above the currenttime
  if (readpointertime < currenttime) {
    // either we have fallen behind or we have wrapped---let's assume wrapped
    if ((readpointertime + ROTATION_PERIOD)
	> currenttime
	+ (32 << currentshift)
	- PWM_ERROR_MARGIN) {
      pwm_set_wrap(1,
		   (readpointertime + ROTATION_PERIOD)
		   - currenttime
		   - (32 << currentshift)
		   + PWM_ERROR_MARGIN);
      pwm_set_counter(1, 0);
      pwm_set_enabled(1, true);
    } else {
      pwm_force_irq(1);
    }
    pwm_set_wrap(2,
		 (readpointertime + ROTATION_PERIOD)
		 - currenttime
		 + PWM_ERROR_MARGIN);
    pwm_set_wrap(3,
		 (readpointertime + ROTATION_PERIOD)
		 - currenttime
		 + (8*rawbytecount << currentshift)
		 + PWM_ERROR_MARGIN);
  } else {
    if (readpointertime
	> currenttime
	+ (32 << currentshift)
	- PWM_ERROR_MARGIN) {
      pwm_set_wrap(1,
		   readpointertime
		   - currenttime
		   - (32 << currentshift)
		   + PWM_ERROR_MARGIN);
      pwm_set_counter(1, 0);
      pwm_set_enabled(1, true);
    } else {
      pwm_force_irq(1);
    }
    pwm_set_wrap(2,
		 readpointertime
		 - currenttime
		 + PWM_ERROR_MARGIN);
    pwm_set_wrap(3,
		 readpointertime
		 - currenttime
		 + (8*rawbytecount << currentshift)
		 + PWM_ERROR_MARGIN);
  }
  pwm_set_counter(2, 0);
  pwm_set_counter(3, 0);
  pwm_set_irq_mask_enabled(0b1110, true);
  pwm_set_enabled(2, true);
  pwm_set_enabled(3, true);
}


// should probably be a macro
// probably always produces a result one too low
// doesn't work on low values of endtime
static inline uint32_t byte_offset(uint32_t endtime, uint32_t targettime, uint32_t length) {
  return (targettime - (endtime - (length*8 << currentshift))) / (8 << currentshift);
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
static void jump_readbuffer(void) {
  struct toc *toc = selecteddrive->tracks[selecteddrive->currenttrack];
  uint32_t targettime = get_currenttime() + ((8 << currentshift) + PWM_ERROR_MARGIN);
  targettime = (targettime / (8 << currentshift)); // the TOC times are in bytes
  uint_fast8_t middle = 0;
  if (targettime < toc->times[0]) {
    readpointer = toc->cdr;
    return;
  }
  // somehow radix is easier for me to understand than the normal way of doing binary search
  for (int i = 3; i; i--) {
    if (targettime > toc->times[middle | (1 << i)]) {
      middle |= (1 << i);
    }
  }
  readpointer = (toc->addresses[middle] << 3) + (void *)trackstorage;
}

// TODO: check other times residualdatabytes are used are correct
static inline void add_data_to_readbuffer(uint8_t *myreadbufferend,
					  uint8_t *data,
					  uint32_t end,
					  uint32_t start) {
  for (uint32_t i = start; i < end; i++) {
    residualdatabytes++;
    residualdata |= (data[i]) << ((4 - residualdatabytes) * 8);
    if (residualdatabytes == 4) {
      readbuffer[*myreadbufferend] = residualdata;
      residualdata = 0;
      residualdatabytes = 0;
      (*myreadbufferend)++;
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
 readstart:
  currenttime = get_currenttime();
  targettime = currenttime + ((8 << currentshift) + PWM_ERROR_MARGIN);
  if (readpointertime > targettime) {
    // we have overshot (unlikely) or the time has wrapped (more likely)
    if (readpointertime > ROTATION_PERIOD + ((8 << currentshift) + PWM_ERROR_MARGIN)) {
      readpointertime -= ROTATION_PERIOD;
    } else {
      jump_readbuffer();
    }
  }
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
      pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
      // set up pwm interrupts
      // interrupt 4 will do whatever interrupt 2 normally would
      pwm_set_wrap(3, readpointertime - currenttime + PWM_ERROR_MARGIN);
      pwm_set_counter(3, 0);
      pwm_set_irq_enabled(3, true);
      pwm_set_enabled(3, true);
      // set status
      status.rawreadstage = EXHAUSTED_FM_AM;
      readpointer = ((struct datablock *)readpointer)->cdr;
      while (((struct bytes *)readpointer)->type == FMAM) {
	// TODO: something when we try to add too much to the fifo
	bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
	// readpointertime will keep increasing as we add more stuff
	pwm_set_wrap(3, readpointertime - currenttime + (8 << currentshift) + PWM_ERROR_MARGIN);
	readpointertime += (8 << currentshift);
	readpointer = ((struct datablock *)readpointer)->cdr;
      }
      if (((struct bytes *)readpointer)->type == MFMAM) {
	// TODO: something when we try to add too much to the fifo
	for (int i = 0; i < 3; i++) {
	  bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	  pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
	}
	// readpointertime will keep increasing as we add more stuff
	pwm_set_wrap(3, readpointertime - currenttime + (24 << currentshift) + PWM_ERROR_MARGIN);
	// this gives us one residual byte
	residualdata = ((struct am *)readpointer)->byte3 << 24;
	residualdatabytes = 1;
	readpointertime += (32 << currentshift);
	// don't forget we're now doing an MFM read (if that's any different)
	status.rawreadstage += EXHAUSTED_MFM_AM;
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
	  pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
	}
	// set up pwm interrupts
	pwm_set_wrap(3, readpointertime - currenttime - (8 << currentshift) + PWM_ERROR_MARGIN);
	pwm_set_counter(3, 0);
	pwm_set_irq_enabled(3, true);
	pwm_set_enabled(3, true);
	// set status
	status.rawreadstage = EXHAUSTED_MFM_AM;
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
  if (targettime >= ROTATION_PERIOD) {
    targettime -= ROTATION_PERIOD;
  }
  // cause pwm 4 to interrupt when we need to start the SMs
  // we can't disable interrupts yet because getcurrenttime relies on them working
  currenttime = get_currenttime();
  DI();
  if (currenttime >= targettime) {
    // oh no
    // actually no give up
    bpassert(false);
    // let's just ignore the problem
    targettime = currenttime + 1;
  }
  pwm_set_wrap(4, targettime - currenttime);
  pwm_set_counter(4, 0);
  pwm_set_irq_enabled(4, true);
  pwm_set_enabled(4, true);
  EI();
  // we want the buffer to be refilled
  irq_set_pending(BUFFERS_IRQ_NUMBER);
}

static inline void flush_residual(uint8_t *myreadbufferend, uint8_t *myreadbufferlength) {
  if (residualdatabytes) {
    readbuffer[*myreadbufferend] = residualdata;
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
    EI();
    if ((status.rawreadstage != EXHAUSTED_FM_AM) && (status.rawreadstage != EXHAUSTED_MFM_AM)) {
      pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
    }
  }
 firsttime:
  switch (((struct bytes *)readpointer)->type) {
  case TOC:
    // start of the disc
    readpointer = ((struct toc *)readpointer)->cdr;
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
      pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
      // set up pwm interrupts
      uint32_t currenttime = (timebasenumber << 16) + pwm_get_counter(0);
      set_rawread_timers(1, currenttime);
      // set status
      status.rawreadstage = WAITING_FM_AM;
      readpointertime += (8 << currentshift);
      readpointer = ((struct datablock *)readpointer)->cdr;
      while (((struct bytes *)readpointer)->type == FMAM) {
	// TODO: something when we try to add too much to the fifo
	bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
	// readpointertime will keep increasing as we add more stuff
	pwm_set_wrap(3, readpointertime - currenttime + (8 << currentshift) + PWM_ERROR_MARGIN);
	readpointertime += (8 << currentshift);
	readpointer = ((struct datablock *)readpointer)->cdr;
      }
      if (((struct bytes *)readpointer)->type == MFMAM) {
	// TODO: something when we try to add too much to the fifo
	for (int i = 3; i; i--) {
	  bpassert(!(pio_sm_is_tx_fifo_full(pio0, 1)));
	  pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
	}
	// readpointertime will keep increasing as we add more stuff
	pwm_set_wrap(3, readpointertime - currenttime + (24 << currentshift) + PWM_ERROR_MARGIN);
	// this gives us one residual byte
	residualdata = ((struct am *)readpointer)->byte3 << 24;
	residualdatabytes = 1;
	readpointertime += (32 << currentshift);
	// don't forget we're now doing an MFM read (if that's any different)
	status.rawreadstage += (WAITING_MFM_AM - WAITING_FM_AM);
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
	pio_sm_put(pio0, 1, ((struct am *)readpointer)->rawbyte);
      }
      // set up pwm interrupts
      uint32_t currenttime = (timebasenumber << 16) + pwm_get_counter(0);
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
  EI();
}

void maintain_buffers(void) {
  deferredtasks.urgent = false;
  // read
  if (deferredtasks.startread) {
    status.reading = true;
    deferredtasks.readmore = true;
    deferredtasks.startread = false;
    // seek
    jump_readbuffer();
    seek_readbuffer();
  }
  // stop
  if (deferredtasks.stop) {
    status.reading = false;
    deferredtasks.stop = false;
    residualdatabytes = 0;
    residualdata = 0;
    readbufferlength = 0;
    // restart and disable state machines
    *(volatile uint32_t *)(PIO0_BASE + PIO_CTRL_OFFSET) = 0b11110000;
    pio_sm_clear_fifos(pio0, 0);
    pio_sm_clear_fifos(pio0, 1);
  }
  // change track - basically the same as stopping and starting again
  if (deferredtasks.changetrack && status.reading) {
    deferredtasks.changetrack = false;
    deferredtasks.readmore = true;
    // clear stuff
    residualdatabytes = 0;
    readbufferlength = 0;
    pio_sm_clear_fifos(pio0, 0);
    pio_sm_clear_fifos(pio0, 1);
    jump_readbuffer();
    seek_readbuffer();
  }
  if (status.reading &&
      deferredtasks.readmore &&
      (status.rawreadstage != WAITING_FM_AM) &&
      (status.rawreadstage != WAITING_MFM_AM)) {
    maintain_readbuffer();
    // just to be safe
    if ((status.rawreadstage != EXHAUSTED_FM_AM) && (status.rawreadstage != EXHAUSTED_MFM_AM)) {
      pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
    }
  }
}
