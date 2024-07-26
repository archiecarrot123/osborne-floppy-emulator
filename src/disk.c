#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "disk.h"
#include "main.h"
#include "memory.h"
#include "buffers.h"

// not really sure why i'm passing around pointers when i plan to make the
// time associated with said pointers a global variable - such is my sleep-deprived state of delerium

// TODO: fix TOC generation - it's not working

uint16_t bytetotal = 0;

volatile struct disk drive1;
volatile struct disk drive2;
volatile struct disk *selecteddrive;

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

static struct datablock * add_big(void *previous, const uint8_t data[67]) {
  struct datablock *block = alloc(DATABLOCK);
  ((struct bytes *)previous)->cdr = block;
  block->type = DATABLOCK;
  memcpy(&(block->data), data, 67);
  bytetotal += 67;
  return block;
}

static struct smalldatablock * add_small(void *previous, const uint8_t data[19]) {
  struct smalldatablock *block = alloc(SMALLDATABLOCK);
  ((struct bytes *)previous)->cdr = block;
  block->type = SMALLDATABLOCK;
  memcpy(&(block->data), data, 19);
  bytetotal += 19;
  return block;
}

static struct tinydatablock * add_tiny(void *previous, const uint8_t data[3]) {
  struct tinydatablock *block = alloc(TINYDATABLOCK);
  ((struct bytes *)previous)->cdr = block;
  block->type = TINYDATABLOCK;
  memcpy(&(block->data), data, 3);
  bytetotal += 3;
  return block;
}

static struct bytes * add_bytes(void *previous, uint8_t byte, uint16_t count) {
  struct bytes *block = alloc(BYTES);
  ((struct bytes *)previous)->cdr = block;
  block->type = BYTES;
  block->byte = byte;
  block->count = count;
  bytetotal += count;
  return block;
}

static struct am * add_fmam(void *previous, uint16_t rawbyte) {
  struct am *block = alloc(FMAM);
  ((struct bytes *)previous)->cdr = block;
  block->type = FMAM;
  block->rawbyte = rawbyte;
  bytetotal += 1;
  return block;
}

static struct am * add_mfmam(void *previous, uint16_t rawbyte, uint8_t byte3) {
  struct am *block = alloc(MFMAM);
  ((struct bytes *)previous)->cdr = block;
  block->type = MFMAM;
  block->rawbyte = rawbyte;
  block->byte3 = byte3;
  bytetotal += 4;
  return block;
}

static struct toc * create_toc(void) {
  struct toc *block = alloc(TOC);
  block->type = TOC;
  return block;
}

static void add_toc_entry(struct toc *toc, unsigned int index, uint16_t time, void *address) {
  toc->times[index] = time;
  toc->addresses[index] = (address - (void *)trackstorage) >> 3;
}

// least optimised thing ever
// hopefully the compiler is smart enough to do this itself
static uint16_t software_crc(const uint8_t *data, size_t length, uint16_t crc) {
  uint8_t mybyte;
  for (unsigned int i = 0; i < length; i++) {
    mybyte = data[i];
    for (unsigned int j = 0; j < 8; j++) {
      if (mybyte % 2) {
	crc ^= CRC_POLYNOMIAL;
      }
      mybyte >>= 1;
      crc <<= 1;
    }
  }
  return crc;
}

// adds the id field and gap 2
static void * add_id(void   *previous,
		     bool    mfm,
		     uint8_t track,
		     bool    side1,
		     uint8_t sector,
		     uint8_t length) {
  void *last;
  uint8_t data[10];
  int crcoffset;
  uint16_t crc;
  if (mfm) {
    last = add_bytes(previous, 0x00, 12);
    last = add_mfmam(last, MFM_IDAM, 0xFE);
    for (int i = 0; i < 3; i++) {
      data[i] = 0xA1;
    }
    data[3] = 0xFE;
    crcoffset = 4;
  } else {
    // sync
    last = add_bytes(previous, 0x00, 6);
    // id address mark
    last = add_fmam(last, FM_IDAM);
    data[0] = 0xFE;
    crcoffset = 1;
  }
  data[crcoffset    ] = track;
  data[crcoffset + 1] = side1 ? 1 : 0;
  data[crcoffset + 2] = sector;
  data[crcoffset + 3] = length;
  crc = software_crc(data, crcoffset + 4, CRC_INITIAL);
  data[crcoffset + 5] = crc >> 8;
  data[crcoffset + 4] = crc & 0xFF;
  last = add_tiny(last, data + crcoffset);
  last = add_tiny(last, data + crcoffset + 3);
  if (mfm) {
    last = add_bytes(last, 0x4E, 22);
  } else {
    last = add_bytes(last, 0xFF, 11);
  }
  return last;
}

// adds some test data and some of gap 3
// the length of gap 3 that it generates is (67 - ((128 << length) % 67)) - 2
// note that gap 3 must be at least 10 bytes in FM and 24 bytes in MFM
// special cases could be added to reduce the length of some of the gaps,
// this would in fact save space when length = 3
// length = 0:  4 bytes
// length = 1: 10 bytes
// length = 2: 22 bytes
// length = 3: 46 bytes
static void * add_test_data(void   *previous,
			    bool    mfm,
			    uint8_t length,
			    bool    deleted) {
  void *last;
  uint8_t data[67];
  uint16_t crc;
  if (mfm) {
    // sync
    last = add_bytes(previous, 0x00, 12);
    // data address mark
    last = add_mfmam(last, MFM_DXM, deleted ? 0xF8 : 0xFB);
    // calculate start of crc
    for (int i = 0; i < 3; i++) {
      data[i] = 0xA1;
    }
    data[3] = 0xFE;
    crc = software_crc(data, 4, CRC_INITIAL);
  } else {
    last = add_bytes(previous, 0x00, 6);
    last = add_fmam(last, deleted ? FM_DEM : FM_DAM);
    data[0] = deleted ? FM_DEM : FM_DAM;
    crc = software_crc(data, 4, CRC_INITIAL);
  }
  // make up some data
  int i;
  for (i = 0; i < 67; i++) {
    data[i] = 0x81 | (i & 2);
  }
  for (i = 66; i < (128 << length); i += 67) {
    last = add_big(last, data);
    crc = software_crc(data, 67, crc);
  }
  // i is now the position vs. the start of the actual data,
  // of the last element in the array called data
  // make it instead the index of the element in the array data that corresponds to the first byte of crc
  // i + 1 - (128 << length) is the number of elements in the array data that do not fall in the actual data
  i = 67 - (i + 1 - (128 << length));
  crc = software_crc(data, i, crc);
  data[i    ] = crc & 0xFF;
  data[i + 1] = crc << 8;
  for (i = i + 2; i < 67; i++) {
    data[i] = mfm ? 0x4E : 0xFF;
  }
  return add_big(last, data);
}

void fill_toc(struct toc *toc);

// generate a disk for testing purposes
// fm
void generate_fm_test_disk(struct disk *disk) {
  struct toc *toc = create_toc();
  void *last;
  disk->mfm = false;
  // preamble
  // gap 0
  last = add_bytes(toc, 0xFF, 40);
  // sync
  last = add_bytes(last, 0x00, 6);
  // index address mark
  last = add_fmam(last, FM_IAM);
  // gap 1 - min 16 bytes in fm 
  last = add_bytes(last, 0xFF, 26);
  // 10 sectors, 299 bytes each
  for (int i = 0; i < 10; i++) {
    // id field and gap 2
    last = add_id(last, false, 0, false, i, 1); // 256 bytes/sector
    // data field and gap 3 (10 bytes)
    last = add_test_data(last, false, 1, true); // 256 bytes/sector, deleted
  }
  // gap 4 - a length of 62 bytes should bring us to a total of 3125 bytes per track
  last = add_bytes(last, 0xFF, 62);
  ((struct bytes *)last)->cdr = toc;
  fill_toc(toc);
  // make the test disk
  disk->mfm = false;
  disk->wp = true; // we don't have writing yet
  // all tracks get track 0's data, to make sure i don't accidentally crash it
  for (int i = 0; i < 40; i++) {
    disk->tracks[i] = toc;
  }
}

// mfm
void generate_mfm_test_disk(struct disk *disk) {
  struct toc *toc = create_toc();
  void *last;
  disk->mfm = true;
  // preamble
  // gap 0
  last = add_bytes(toc, 0x4E, 80);
  // sync
  last = add_bytes(last, 0x00, 12);
  // index address mark
  last = add_mfmam(last, MFM_IAM, 0xFC);
  // gap 1 - min 32 bytes in mfm 
  last = add_bytes(last, 0x4E, 50);
  // 5 sectors, 299 bytes each
  for (int i = 0; i < 5; i++) {
    // id field and gap 2
    last = add_id(last, true, 0, false, i, 3); // 1024 bytes/sector
    // data field and gap 3 (46 bytes)
    last = add_test_data(last, true, 3, true); // 1024 bytes/sector, deleted
  }
  // gap 4 - a length of 62 bytes should bring us to a total of 6250 bytes per track
  last = add_bytes(last, 0x4E, 62);
  ((struct bytes *)last)->cdr = toc;
  fill_toc(toc);
  // make the test disk
  disk->mfm = false;
  disk->wp = true; // we don't have writing yet
  // all tracks get track 0's data, to make sure i don't accidentally crash it
  for (int i = 0; i < 40; i++) {
    disk->tracks[i] = toc;
  }
}

// need a function to generate a TOC
void fill_toc(struct toc *toc) {
  struct bytes *seekpointer = toc->cdr;
  uint16_t bytecount = 0;
  uint_fast16_t blockcount = 0;
  uint_fast16_t blocktotal = 0;
  uint_fast16_t blockthreshold;
  while (seekpointer->type != TOC) {
    seekpointer = seekpointer->cdr;
    blocktotal++;
  }
  bpassert(blocktotal);
  // seekpointer should contain toc
  for (uint_fast8_t i = 0; i < 16; i++) {
    blockthreshold = ((i + 1)*blocktotal) / 17;
    while (blockcount < blockthreshold - 1) {
      blockcount++;
      seekpointer = seekpointer->cdr;
      switch (seekpointer->type) {
      case DATABLOCK:
	bytecount += 67;
	break;
      case SMALLDATABLOCK:
	bytecount += 19;
	break;
      case TINYDATABLOCK:
	bytecount += 3;
	break;
      case BYTES:
	bytecount += seekpointer->count;
	break;
      case FMAM:
	bytecount += 1;
	break;
      case MFMAM:
	bytecount += 4;
	break;
      default:
	// shouldn't happen
	asm volatile ("bkpt 0x02");
	break;
      }
    }
    add_toc_entry(toc, i, bytecount, seekpointer->cdr);
  }
}
