#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "memory.h"


struct disk {
  bool enabled;
  bool selected;
  bool mfm;
  bool wp;
  uint_fast8_t currenttrack;
  struct toc *tracks[40];
};


extern volatile struct disk drive1;
extern volatile struct disk drive2;
extern volatile struct disk *selecteddrive;

void generate_fm_test_disk(struct disk *disk);

void fill_toc(struct toc *toc);
