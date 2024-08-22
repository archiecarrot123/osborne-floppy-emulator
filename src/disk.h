#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "memory.h"


// remember: MSB-first
#define MFM_IAM  0b0101001000100100 // C2 without clock transition between bits 3 and 4
#define MFM_IDAM 0b0100010010001001 // A1 without clock transition between bits 4 and 5
#define MFM_DXM  0b0100010010001001 // A1 without clock transition between bits 4 and 5
#define FM_IAM   0b1111011101111010 // FC with clock D7
#define FM_IDAM  0b1111010101111110 // FE with clock C7
#define FM_DAM   0b1111010101101111 // FB with clock C7
#define FM_DEM   0b1111010101101010 // F8 with clock C7

#define CRC_POLYNOMIAL 0x1021 // CRC-16-CCITT
#define CRC_INITIAL    0xFFFF

// should be enough to allocate an entire mfm track
// 80 for data, 4/9 for index, 40/9 for ams, plus say one more, means it should be at least 86
#define MIN_FREE_BIGBLOCKS 100
// should be at least a track greater than MIN_FREE_BIGBLOCKS
#define MAX_FREE_BIGBLOCKS (2*MIN_FREE_BIGBLOCKS)


// firsttrack and lasttrack are the first and last tracks loaded
struct disk {
  bool enabled  : 1;
  bool selected : 1;
  bool mfm      : 1;
  bool wp       : 1;
  uint_fast8_t currenttrack    : 6;
  uint_fast8_t firsttrack      : 6;
  uint_fast8_t lasttrack       : 6;
  uint_fast8_t sectorspertrack : 5;
  uint16_t diskid;
  struct toc *tracks[40];
};

// up to 1024 bytes
// processor 1 is responsible for calculating the crcs (using the DMA)
// when it copies data from the partial disk image
// loaded in memory into this struct
struct sector {
  bool mfm     : 1;
  bool side1   : 1;
  bool deleted : 1;
  uint_fast8_t sector : 5;
  uint_fast8_t track  : 6;
  uint_fast8_t length : 2;
  uint8_t crc[2];
  uint8_t data[1024];
};

union trackrequest {
  struct {
    uint16_t diskid;
    uint8_t  trackno     : 6;
    bool     side1       : 1;
    uint8_t  sectorcount : 5;
  };
  uint32_t asword;
};
_Static_assert((sizeof(union trackrequest) == 4),
	       "trackrequest size not 4 bytes");


extern volatile struct disk drive1;
extern volatile struct disk drive2;
extern volatile struct disk *selecteddrive;

void generate_fm_test_disk(struct disk *disk);
void generate_mfm_test_disk(struct disk *disk);

void fill_toc(struct toc *toc);

void add_sector_to_disk(struct sector *sector);

void maintain_tracks(void);
