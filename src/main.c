#include "main.h"
#include "memory.h"
#include "readwrite.pio.h"
#include "buffers.h"
#include "disk.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pwm.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/intctrl.h"
#include "hardware/regs/pio.h"
#include "hardware/structs/sio.h"
#include "hardware/structs/clocks.h"


volatile uint_fast8_t timebasenumber;
// we start in mfm
uint_fast8_t currentperiod = MFM_PERIOD;

volatile struct deferredtasks deferredtasks = {
  false,
  false,
  false,
  false,
  false,
  false,
  false,
  false
};

volatile struct status status = {
  false,
  false,
  true,  // the emulator starts in mfm mode
  false,
  NO_RAW_READ
};

// used for rawread so the data doesn't have to be word-aligned
uint_fast8_t lastwordbytecount;

struct piooffsets piooffsets;
struct pioconfigs pioconfigs;

// timebase should use CH0,
// the raw timer should use CH1 and CH2
// both CH1 and CH2 should be set elsewhere
void pwm_irq_handler(void) {
  uint32_t interrupts = *(volatile uint32_t *)(PWM_BASE + PWM_INTS_OFFSET);
  if (interrupts & PWM_INTS_CH1_BITS) {
    // change the amount to output
    *(volatile uint32_t *)(PIO0_BASE + PIO_SM0_SHIFTCTRL_OFFSET) |=
      (8 * lastwordbytecount) << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB;
    // disable ourselves
    pwm_set_irq_enabled(1, false);
    pwm_set_enabled(1, false);
    pwm_clear_irq(1);
  }
  if (interrupts & PWM_INTS_CH2_BITS) {
    // restore amount to output
    *(volatile uint32_t *)(PIO0_BASE + PIO_SM0_SHIFTCTRL_OFFSET) &=
      ~(0b11111 << PIO_SM0_SHIFTCTRL_PULL_THRESH_LSB);
    // status update
    status.rawreadstage++;
    bpassert((status.rawreadstage == ONGOING_FM_AM) ||
	     (status.rawreadstage == ONGOING_MFM_AM));
    // enable fifo interrupt to get it refilled
    pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
    // disable ourselves
    pwm_set_irq_enabled(2, false);
    pwm_set_enabled(2, false);
    pwm_clear_irq(2);
  }
  if (interrupts & PWM_INTS_CH3_BITS) {
    // this is only a status update - now we can put stuff in rawread's fifo again
    status.rawreadstage = NO_RAW_READ;
    // disable ourselves
    pwm_set_irq_enabled(3, false);
    pwm_set_enabled(3, false);
    pwm_clear_irq(3);
  }
  if (interrupts & PWM_INTS_CH4_BITS) {
    // jump state machines
    if (status.rawreadstage) {
      if (status.mfm) {
	// first bit 1 us after being started
	// conditionless jump with no sideset or delay is conveniently just the address
	pio_sm_exec(pio0, 1, piooffsets.rawread + 1);
	pio_sm_exec(pio0, 0, piooffsets.read + 8);
      } else {
	// first bit 0.5 us after being started
	pio_sm_exec(pio0, 1, piooffsets.rawread + 1);
	pio_sm_exec(pio0, 0, piooffsets.read + 8);
      }
      status.rawreadstage++;
    } else {
      if (status.mfm) {
	// first bit 0.75 us after being started
	pio_sm_exec(pio0, 0, piooffsets.read + 9);
	pio_sm_exec(pio0, 1, piooffsets.rawread + 6);
      } else {
	// first bit 0 us after being started
	pio_sm_exec(pio0, 0, piooffsets.read + 2);
	pio_sm_exec(pio0, 1, piooffsets.rawread + 6);
      }
    }
    // clear interrupts
    *(volatile uint32_t *)(PIO0_BASE + PIO_IRQ_OFFSET) = 0b00110000;
    // restart and enable state machines
    *(volatile uint32_t *)(PIO0_BASE + PIO_CTRL_OFFSET) = 0b00110011;
    // enable state machine fifo interrupt
    pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, true);
    // re-enable the read signal
    gpio_set_outover(13, GPIO_OVERRIDE_NORMAL);
    // disable ourselves
    pwm_set_irq_enabled(4, false);
    pwm_set_enabled(4, false);
    pwm_clear_irq(4);
  }
  if (interrupts & PWM_INTS_CH0_BITS) {
    timebasenumber++;
    if (timebasenumber == TIMEBASE_PER_ROTATION) {
      timebasenumber = 0;
      if (status.selected) {
	// set index pulse
	gpio_put(2, true);
      }
      // we set up the interrupt anyway because we might be selected in the mean time
      pwm_set_wrap(5, INDEX_PULSE_LENGTH - 1);
      pwm_set_counter(5, 0);
      pwm_set_irq_enabled(5, true);
      pwm_set_enabled(5, true);
      pwm_set_wrap(0, 65535);
    } else if (timebasenumber == TIMEBASE_PER_ROTATION - 1) {
      pwm_set_wrap(0, TIMEBASE_REMAINDER - 1);
    }
    pwm_clear_irq(0);
  }
  if (interrupts & PWM_INTS_CH5_BITS) {
    // clear index pulse
    gpio_put(2, false);
    pwm_set_irq_enabled(5, false);
    pwm_set_enabled(5, false);
    pwm_clear_irq(5);
  }
}

static inline void update_mode(void) {
  // TODO: write
  if (status.mfm) {
    currentperiod = MFM_PERIOD;
    pio_remove_program(pio0, &readFM_irq_program, piooffsets.read);
    pio_remove_program(pio0, &rawreadFM_irq_program, piooffsets.rawread);
    pio_add_program_at_offset(pio0, &readMFM2_irq_program, piooffsets.read);
    pio_add_program_at_offset(pio0, &rawreadMFM_irq_program, piooffsets.rawread);
    pio_sm_set_config(pio0, 0, &pioconfigs.mfmread);
    pio_sm_set_config(pio0, 1, &pioconfigs.mfmrawread);
    pio_clkdiv_restart_sm_mask(pio0, 0b1011);
  } else {
    currentperiod = FM_PERIOD;
    pio_remove_program(pio0, &readMFM2_irq_program, piooffsets.read);
    pio_remove_program(pio0, &rawreadMFM_irq_program, piooffsets.rawread);
    pio_add_program_at_offset(pio0, &readFM_irq_program, piooffsets.read);
    pio_add_program_at_offset(pio0, &rawreadFM_irq_program, piooffsets.rawread);
    pio_sm_set_config(pio0, 0, &pioconfigs.fmread);
    pio_sm_set_config(pio0, 1, &pioconfigs.fmrawread);
    pio_clkdiv_restart_sm_mask(pio0, 0b1011);
  }
}

static inline void select_drive(volatile struct disk *drive) {
  drive->selected = true;
  // begin read if we are being this drive
  if (drive->enabled) {
    status.selected = true;
    selecteddrive = drive;
    if (drive->mfm != status.mfm) {
      status.mfm = drive->mfm;
      update_mode();
    }
    deferredtasks.urgent = true;
    deferredtasks.startread = true;
    irq_set_pending(BUFFERS_IRQ_NUMBER);
    if (*(volatile uint32_t *)(PWM_BASE + PWM_CH5_CSR_OFFSET) & 0b1) {
      // if this pwm is enabled then there should be an index pulse
      gpio_put(2, true);
    }
    if (drive->wp) {
      // set write protect
      gpio_put(12, true);
    }
    if (selecteddrive->currenttrack == 0) {
      // set track 0
      gpio_put(11, true);
    }
  }
}

static inline void deselect_drive(volatile struct disk *drive) {
    // stop read if we're being this drive
    if (drive->enabled) {
      status.selected = false;
      deferredtasks.urgent = true;
      deferredtasks.stop = true;
      deferredtasks.startread = false;
      irq_set_pending(BUFFERS_IRQ_NUMBER);
      // clear index pulse
      gpio_put(2, false);
      // clear wp
      gpio_put(12, false);
      // clear track 0
      gpio_put(11, false);
      // make sure we stop the read signal
      // note that this must be low regardless of the polarity of the signal,
      // as a high level will assert the line, preventing the actual drives from deasserting it
      gpio_set_outover(13, GPIO_OVERRIDE_LOW);
    }
    drive->selected = false;
    // obviously no drive is selected, unless something has gone horribly wrong
    selecteddrive = NULL;
}

// we'll want to listen to interrupts on GPIOs 3, 5, 8, and 10
// these are respectively connected to DS 1, DS 2, Step, and Write Gate
void gpio_irq_handler(void) {
  // confusingly by low i mean in position, not edge
  uint32_t lowinterrupts = *(volatile uint32_t *)(IO_BANK0_BASE + IO_BANK0_PROC0_INTS0_OFFSET);
  if (lowinterrupts & IO_BANK0_INTR0_GPIO3_EDGE_LOW_BITS) {
    gpio_acknowledge_irq(3, GPIO_IRQ_EDGE_FALL);
    select_drive(&drive1);
  }
  if (lowinterrupts & IO_BANK0_INTR0_GPIO3_EDGE_HIGH_BITS) {
    gpio_acknowledge_irq(3, GPIO_IRQ_EDGE_RISE);
    //deselect_drive(&drive1);
  }
  if (lowinterrupts & IO_BANK0_INTR0_GPIO5_EDGE_LOW_BITS) {
    gpio_acknowledge_irq(5, GPIO_IRQ_EDGE_FALL);
    select_drive(&drive2);
  }
  if (lowinterrupts & IO_BANK0_INTR0_GPIO5_EDGE_HIGH_BITS) {
    gpio_acknowledge_irq(5, GPIO_IRQ_EDGE_RISE);
    deselect_drive(&drive2);
  }
  uint32_t highinterrupts = *(volatile uint32_t *)(IO_BANK0_BASE + IO_BANK0_PROC0_INTS1_OFFSET);
  if (highinterrupts & IO_BANK0_INTR1_GPIO8_EDGE_LOW_BITS) {
    gpio_acknowledge_irq(8, GPIO_IRQ_EDGE_FALL);
    // change currenttrack depending on direction
    if (selecteddrive) {
      if (gpio_get(7)) {
	// step out
	if (selecteddrive->currenttrack != 0) {
	  selecteddrive->currenttrack--;
	}
	if (selecteddrive->currenttrack == 0) {
	  gpio_put(11, true);
	}
      } else {
	// step in
	if (selecteddrive->currenttrack == 0) {
	  gpio_put(11, false);
	}
	if (selecteddrive->currenttrack != 39) {
	  selecteddrive->currenttrack++;
	}
      }
      deferredtasks.urgent = true;
      deferredtasks.changetrack = true;
      irq_set_pending(BUFFERS_IRQ_NUMBER);
    }
  }
  if (highinterrupts & IO_BANK0_INTR1_GPIO10_EDGE_LOW_BITS) {
    gpio_acknowledge_irq(10, GPIO_IRQ_EDGE_FALL);
    // begin write if we're selected
  }
}

// reading should use irq0, sm0 and sm1
// sm0 for data read, sm1 for raw read
// sm1 should be managed by a pwm timer, so we won't worry about it here
void pio0_irq0_handler(void) {
  uint32_t interrupts = *(volatile uint32_t *)(PIO0_BASE + PIO_IRQ0_INTS_OFFSET);
  if (interrupts & PIO_INTR_SM0_TXNFULL_BITS) {
    // fill buffer
    // we assume that the interrupt will trigger again if the condition persists
    if (readbufferlength) {
      pio_sm_put(pio0, 0, readbuffer[readbufferstart]);
      readbufferstart++;
      readbufferlength--;
      deferredtasks.readmore = true;
      irq_set_pending(BUFFERS_IRQ_NUMBER);
    } else {
      if ((status.rawreadstage == WAITING_FM_AM) ||
	  (status.rawreadstage == WAITING_MFM_AM)) {
	// disable the interrupt and status update
	pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, false);
	status.rawreadstage++;
      } else {
	// we have run out of readbuffer but the fifo is not empty yet
	// even if the fifo does run out we should still try to recover,
	// as the FDC might not be reading or might retry
	deferredtasks.readmore = true;
	deferredtasks.urgent = true;
	pio_set_irq0_source_enabled(pio0, pis_sm0_tx_fifo_not_full, false);
	irq_set_pending(BUFFERS_IRQ_NUMBER);
      }
    }
  }
}

// writing should use irq1, sm3
void pio0_irq1_handler(void) {
  uint32_t interrupts = *(volatile uint32_t *)(PIO0_BASE + PIO_IRQ1_INTS_OFFSET);
  if (interrupts & PIO_INTR_SM3_RXNEMPTY_BITS) { // should always be true
    // empty buffer
    if (writebufferlength < 255) {
      writebufferend++;
      writebuffer[writebufferend] = pio_sm_get(pio0, 3);
      writebufferlength++;
      deferredtasks.writemore = true;
      irq_set_pending(BUFFERS_IRQ_NUMBER);
    } else {
      // :(
    }
  }
}

void proc0_sio_irq_handler(void) {
  if (multicore_fifo_rvalid()) {
    add_sector_to_disk(*(struct sector *volatile *)(sio_hw->fifo_rd));
  }
}

// starts by loading MFM programs as it assumes they are bigger
static void setup_pio(void) {
  piooffsets.rawwrite = pio_add_program(pio0, &write_program);
  piooffsets.read = pio_add_program(pio0, &readMFM2_irq_program);
  piooffsets.rawread = pio_add_program(pio0, &rawreadMFM_irq_program);
  
  pioconfigs.rawwrite = write_program_get_default_config(piooffsets.rawwrite);
  pioconfigs.fmread = readFM_irq_program_get_default_config(piooffsets.read);
  pioconfigs.mfmread = readMFM2_irq_program_get_default_config(piooffsets.read);
  pioconfigs.fmrawread = rawreadFM_irq_program_get_default_config(piooffsets.rawread);
  pioconfigs.mfmrawread = rawreadMFM_irq_program_get_default_config(piooffsets.rawread);

  // we don't set writeraw's clockdiv as it will be different for fm and mfm
  sm_config_set_clkdiv(&pioconfigs.fmread, 96/2);
  sm_config_set_clkdiv(&pioconfigs.mfmread, 96/4);
  sm_config_set_clkdiv(&pioconfigs.fmrawread, 96/2);
  sm_config_set_clkdiv(&pioconfigs.mfmrawread, 96/4);

  sm_config_set_fifo_join(&pioconfigs.rawwrite, PIO_FIFO_JOIN_RX);
  sm_config_set_fifo_join(&pioconfigs.fmread, PIO_FIFO_JOIN_TX);
  sm_config_set_fifo_join(&pioconfigs.mfmread, PIO_FIFO_JOIN_TX);
  sm_config_set_fifo_join(&pioconfigs.fmrawread, PIO_FIFO_JOIN_TX);
  sm_config_set_fifo_join(&pioconfigs.mfmrawread, PIO_FIFO_JOIN_TX);
  
  sm_config_set_in_pins(&pioconfigs.rawwrite, 9);
  sm_config_set_sideset_pins(&pioconfigs.fmread, 13);
  sm_config_set_out_pins(&pioconfigs.fmread, 13, 1);
  sm_config_set_sideset_pins(&pioconfigs.mfmread, 13);
  sm_config_set_out_pins(&pioconfigs.mfmread, 13, 1);
  sm_config_set_sideset_pins(&pioconfigs.fmrawread, 13);
  sm_config_set_out_pins(&pioconfigs.fmrawread, 13, 1);
  sm_config_set_sideset_pins(&pioconfigs.mfmrawread, 13);
  sm_config_set_out_pins(&pioconfigs.mfmrawread, 13, 1);

  sm_config_set_in_shift(&pioconfigs.rawwrite, false, true, 32);
  sm_config_set_out_shift(&pioconfigs.fmread, false, true, 32);
  sm_config_set_out_shift(&pioconfigs.mfmread, false, true, 32);
  sm_config_set_out_shift(&pioconfigs.fmrawread, false, true, 16);
  sm_config_set_out_shift(&pioconfigs.mfmrawread, false, true, 16);

  sm_config_set_mov_status(&pioconfigs.fmread, STATUS_TX_LESSTHAN, 1);
  sm_config_set_mov_status(&pioconfigs.mfmread, STATUS_TX_LESSTHAN, 1);
  sm_config_set_mov_status(&pioconfigs.fmrawread, STATUS_TX_LESSTHAN, 1);
  sm_config_set_mov_status(&pioconfigs.mfmrawread, STATUS_TX_LESSTHAN, 1);
  
  pio_gpio_init(pio0, 13);

  pio_sm_set_consecutive_pindirs(pio0, 2, 13, 1, true);

  // TODO: write
  pio_sm_set_config(pio0, 0, &pioconfigs.mfmread);
  pio_sm_set_config(pio0, 1, &pioconfigs.mfmrawread);
  pio_clkdiv_restart_sm_mask(pio0, 0b1011);
}

static void setup_gpio(void) {
  // pull up inputs (skipping unused ones)
  gpio_pull_up(3);  // DS 1
  gpio_pull_up(5);  // DS 2
  gpio_pull_up(7);  // direction
  gpio_pull_up(8);  // step
  gpio_pull_up(9);  // write data
  gpio_pull_up(10); // write gate
  // set functions on outputs
  gpio_init(2);  // index
  gpio_init(4);  // DS 1 enable
  gpio_init(6);  // DS 2 enable
  gpio_init(11); // track 0
  gpio_init(12); // write protect
  // set up interrupts
  gpio_set_irq_enabled(3, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
  gpio_set_irq_enabled(5, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true);
  gpio_set_irq_enabled(8, GPIO_IRQ_EDGE_FALL, true);
  // default values and directions
  gpio_clr_mask(0b0001100001010100);
  gpio_set_dir_out_masked(0b0001100001010100);
}

static void setup_timebase(void) {
  pwm_set_wrap(0, 65535);
  timebasenumber = 0;
  pwm_set_clkdiv(0, TIMEBASE_DIVIDER);
  pwm_set_enabled(0, true);
  pwm_set_irq_enabled(0, true);
  // set other pwms' dividers
  pwm_set_clkdiv(1, TIMEBASE_DIVIDER);
  pwm_set_clkdiv(2, TIMEBASE_DIVIDER);
  pwm_set_clkdiv(3, TIMEBASE_DIVIDER);
  pwm_set_clkdiv(4, TIMEBASE_DIVIDER);
  pwm_set_clkdiv(5, TIMEBASE_DIVIDER);
}

static void setup_interrupts(void) {
  irq_set_exclusive_handler(PWM_IRQ_WRAP, pwm_irq_handler);
  irq_set_exclusive_handler(IO_IRQ_BANK0, gpio_irq_handler);
  irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq0_handler);
  irq_set_exclusive_handler(PIO0_IRQ_1, pio0_irq1_handler);
  irq_set_enabled(PWM_IRQ_WRAP, true);
  irq_set_enabled(IO_IRQ_BANK0, true);
  irq_set_enabled(PIO0_IRQ_0, true);
  irq_set_enabled(PIO0_IRQ_1, true);
  // some OS kinda stuff... idk what to call it
  irq_set_exclusive_handler(31, maintain_buffers);
  irq_set_enabled(31, true);
  irq_set_priority(31, 0xC0); // lowest priority
  EI();
}

void main(void) {
  initialize_track_storage();
  generate_mfm_test_disk((struct disk *)&drive1);
  drive1.enabled = true;
  setup_pio();
  setup_gpio();
  setup_timebase();
  setup_interrupts();
  // set DS enables
  gpio_put(4, !drive1.enabled);
  gpio_put(6, !drive2.enabled);
  select_drive(&drive1);
  // testing
  clocks_hw->fc0.src = 0x05;
  gpio_init(16);
  gpio_set_dir(16, true);
  while (1) {
    if (clocks_hw->fc0.status & CLOCKS_FC0_STATUS_DONE_BITS) {
      if ((clocks_hw->fc0.result >> 5) > 1000) {
	gpio_put(16, true);
      } else {
	gpio_put(16, false);
      }
      clocks_hw->fc0.src = 0x05;
    }
    //maintain_buffers();
    maintain_track_storage();
  }
}
