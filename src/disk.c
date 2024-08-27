#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "disk.h"
#include "main.h"
#include "memory.h"
#include "buffers.h"

#include "hardware/structs/sio.h"
#include "pico/multicore.h"

// should probably use bytetotal for asserts

// TODO: use bytetotal to automatically determine gap length

// TODO: be able to load empty disks

volatile uint16_t bytetotal;

volatile struct disk drive1;
volatile struct disk drive2;
volatile struct disk *selecteddrive;

void * volatile trackloadingpointer;
volatile int trackloadingsectorsleft;
volatile bool trackloadingready;
volatile struct sector *trackloadingqueue[10];
volatile uint_fast8_t trackloadingqueuehead;
volatile uint_fast8_t trackloadingqueuetail;

static struct datablock * add_big(void *previous, const uint8_t data[67]) {
  struct datablock *block = alloc(DATABLOCK);
  ((struct bytes *)previous)->cdr = block;
  block->type = DATABLOCK;
  // i bloody well hope memcpy uses the dma
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
  // rough heuristic, actual limit is probably more like 1020
  bpassert(count < 1000);
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

// optimised bitwise method, bytewise method would be faster, but we
// won't be using this very often as we will use the DMA to calculate CRCs
// as we copy memory
static inline uint32_t software_crc_step(uint32_t buffer, uint8_t byte) {
  buffer |= byte;
  for (unsigned int j = 0; j < 8; j++) {
    buffer <<= 1;
    if (buffer & 0x01000000) {
      buffer ^= CRC_POLYNOMIAL << 8;
    }
  }
  return buffer;
}
  
static uint16_t software_crc(const uint8_t *data, size_t length, uint16_t crc) {
  uint32_t buffer = crc << 8;
  for (unsigned int i = 0; i < length; i++) {
    buffer = software_crc_step(buffer, data[i]);
  }
  for (unsigned int i = 0; i < 2; i++) {
    buffer = software_crc_step(buffer, 0x00);
  }
  return buffer >> 8;
}

// adds the id field and gap 2
static void * add_id(void   *previous,
		     bool    mfm,
		     uint8_t track,
		     bool    side1,
		     uint8_t sector,
		     uint8_t length) {
  void *last;
  uint8_t data[6];
  uint16_t crc;
  if (mfm) {
    last = add_bytes(previous, 0x00, 12);
    last = add_mfmam(last, MFM_IDAM, 0xFE);
  } else {
    // sync
    last = add_bytes(previous, 0x00, 6);
    // id address mark
    last = add_fmam(last, FM_IDAM);
  }
  crc = 0xE10E; // precomputed 2024-07-26
  data[0] = track;
  data[1] = side1 ? 1 : 0;
  data[2] = sector;
  data[3] = length;
  crc = software_crc(data, 4, crc);
  data[4] = crc >> 8;
  data[5] = crc & 0xFF;
  last = add_tiny(last, data);
  last = add_tiny(last, data + 3);
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
  } else {
    last = add_bytes(previous, 0x00, 6);
    last = add_fmam(last, deleted ? FM_DEM : FM_DAM);
  }
  crc = deleted ? 0xE108 : 0xE10B; // precomputed 2024-07-26
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
// they will misbehave if there is an attempt to free their tracks
// fm
void generate_fm_test_disk(struct disk *disk) {
  struct toc *toc = create_toc();
  void *last;
  disk->mfm = false;
  bytetotal = 0;
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
  bpassert(3125 - bytetotal == 62);
  last = add_bytes(last, 0xFF, 62);
  ((struct bytes *)last)->cdr = toc;
  fill_toc(toc);
  // make the test disk
  disk->wp = true; // we don't have writing yet
  // all tracks get track 0's data, to make sure i don't accidentally crash it
  for (int i = 0; i < 40; i++) {
    disk->tracks[i] = toc;
  }
  disk->firsttrack = 0;
  disk->lasttrack = 39;
}

// mfm
void generate_mfm_test_disk(struct disk *disk) {
  struct toc *toc = create_toc();
  void *last;
  disk->mfm = true;
  bytetotal = 0;
  // preamble
  // gap 0
  last = add_bytes(toc, 0x4E, 80);
  // sync
  last = add_bytes(last, 0x00, 12);
  // index address mark
  last = add_mfmam(last, MFM_IAM, 0xFC);
  // gap 1 - min 32 bytes in mfm 
  last = add_bytes(last, 0x4E, 50);
  // 5 sectors, 1132 bytes each
  for (int i = 0; i < 5; i++) {
    // id field and gap 2
    last = add_id(last, true, 0, false, i, 3); // 1024 bytes/sector
    // data field and gap 3 (46 bytes)
    last = add_test_data(last, true, 3, true); // 1024 bytes/sector, deleted
  }
  // gap 4 - a length of 444 bytes should bring us to a total of 6250 bytes per track
  bpassert(6250 - bytetotal == 444);
  last = add_bytes(last, 0x4E, 444);
  ((struct bytes *)last)->cdr = toc;
  fill_toc(toc);
  // make the test disk
  disk->wp = true; // we don't have writing yet
  // all tracks get track 0's data, to make sure i don't accidentally crash it
  for (int i = 0; i < 40; i++) {
    disk->tracks[i] = toc;
  }
  disk->firsttrack = 0;
  disk->lasttrack = 39;
}

static unsigned int next_contiguous_bytes(uint8_t data[], unsigned int end) {
  uint8_t byte = data[0];
  unsigned int count = 1;
  for (unsigned int i = 1; i < end; i++) {
    if (data[i] == byte) {
      if (count > 3) {
	return i - count;
      }
      count++;
    } else {
      byte = data[i];
      count = 1;
    }
  }
  return end;
}

// gap3length is a minimum
static void * add_sector_data(void *previous, struct sector sector, unsigned int gap3length) {
  void *last = previous;
  // copy data into buffer
  unsigned int i = 0;
  unsigned int remaining = (128 << sector.length);
#if 0
  while (1) {
    unsigned int highentropy = next_contiguous_bytes(&(sector.data[i]), remaining - i);
    if (highentropy == remaining
	|| remaining < 67) {
      // use biggest ones forever
      while (remaining >= 67) {
	last = add_big(last, sector.data + i);
	remaining -= 67;
	i += 67;
      }
      if (remaining + 2 > 2*19 + 2*3 || // best option is the big one
	  remaining + 2 > 2*19) { // relaxed because i'm too lazy to do extra coding
	struct datablock *block = alloc(DATABLOCK);
	((struct datablock *)last)->cdr = block;
	block->type = DATABLOCK;
	memcpy(block->data, sector.data + i, remaining);
	memcpy(block->data + remaining, sector.crc, 2);
	memset(block->data + remaining + 2, sector.mfm ? 0x4E : 0xFF, 67 - remaining - 2);
	bytetotal += 67;
	last = block;
	if (gap3length > 67 - remaining - 2) {
	  if ((gap3length - (67 - remaining - 2)) < 3) {
	    last = add_bytes(last, sector.mfm ? 0x4E : 0xFF, 3);
	  } else {
	    last = add_bytes(last, sector.mfm ? 0x4E : 0xFF, gap3length - (67 - remaining - 2));
	  }
	}
	return last;
      }
      
    } else if (highentropy == 0) {
      // use bytes type
      unsigned int j;
      uint8_t byte = sector.data[i];
      for (j = i + 1; j < remaining; j++) {
	if (sector.data[j] != byte) {
	  break;
	}
      }
      // need a mechanism to split this
      if (j - i < 900) {
	last = add_bytes(last, byte, j - i);
      } else {
	last = add_bytes(last, byte, 768);
	last = add_bytes(last, byte, j - i - 768);
      }
      remaining -= (j - i);
      i = j;
    } else /* if (highentropy >= 67) */ {
      // use a big one
      last = add_big(last, sector.data + i);
      remaining -= 67;
      i += 67;
    }
  }
#endif
  // old way
  for (remaining = (128 << sector.length); remaining >= 67; remaining -= 67) {
    last = add_big(last, sector.data + i);
    i += 67;
  }
  // we ignore the case remaining < 2 as remaining != 0 by the fundemental theorem of algebra,
  // and remaining = 1 doesn't really come up - it might be impossible
  // this is a modified version of add_big
  struct datablock *block = alloc(DATABLOCK);
  ((struct datablock *)last)->cdr = block;
  block->type = DATABLOCK;
  memcpy(block->data, sector.data + i, remaining);
  memcpy(block->data + remaining, sector.crc, 2);
  memset(block->data + remaining + 2, sector.mfm ? 0x4E : 0xFF, 67 - remaining - 2);
  bytetotal += 67;
  last = block;
  if (gap3length) {
    bpassert((gap3length - (67 - remaining - 2)) >= 0);
    if (gap3length - (67 - remaining - 2)) {
      bpassert((gap3length - (67 - remaining - 2)) > 2);
      last = add_bytes(last, sector.mfm ? 0x4E : 0xFF, gap3length - (67 - remaining - 2));
    }
  }
  return last;
}

static void * add_sector(void *previous, struct sector sector, unsigned int gap3length) {
  void *last;
  // id field and gap 2
  last = add_id(previous, sector.mfm, sector.track, sector.side1, sector.sector, sector.length);
  // data field
  if (sector.mfm) {
    // sync
    last = add_bytes(last, 0x00, 12);
    // data address mark
    last = add_mfmam(last, MFM_DXM, sector.deleted ? 0xF8 : 0xFB);
  } else {
    last = add_bytes(last, 0x00, 6);
    last = add_fmam(last, sector.deleted ? FM_DEM : FM_DAM);
  }
  return add_sector_data(last, sector, gap3length);
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
    while ((blockcount < blockthreshold - 1) && blockthreshold) {
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

void check_consistency(struct toc *toc) {
  // temporary check
  struct bytes *seekpointer = toc->cdr;
  unsigned int bytecount = 0;
  while (seekpointer) {
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
	break;
      }
      seekpointer = seekpointer->cdr;
    }
  bpassert(bytecount == bytetotal);
}

void add_sector_to_disk(struct sector *sector) {
  bpassert(trackloadingsectorsleft);
  if (trackloadingready) {
    void *oldtrackloadingpointer = trackloadingpointer;
    trackloadingpointer = add_sector(trackloadingpointer, *sector, 0);
    bpassert(trackloadingpointer != oldtrackloadingpointer);
    trackloadingsectorsleft--;
  } else {
    bpassert(trackloadingqueuehead < 10);
    trackloadingqueue[trackloadingqueuehead] = sector;
    trackloadingqueuehead++;
  }
}

// blocks until it finishes
static struct toc * load_track(union trackrequest request, bool mfm) {
  struct toc *toc;
  void *last;
  // kindly request that the other processor find the sectors
  trackloadingready = false;
  trackloadingsectorsleft = request.sectorcount;
  trackloadingqueuehead = 0;
  trackloadingqueuetail = 0;
  bpassert(multicore_fifo_wready());
  multicore_fifo_push_blocking(request.asword);
  // is this needed?
  asm volatile ("SEV");
  toc = create_toc();
  bytetotal = 0;
  // preamble
  // gap 0
  last = add_bytes(toc, mfm ? 0x4E : 0xFF, mfm ? 80 : 40);
  // sync
  last = add_bytes(last, 0x00, mfm ? 12 : 6);
  // index address mark
  if (mfm) {
    last = add_mfmam(last, MFM_IAM, 0xFC);
  } else {
    last = add_fmam(last, FM_IAM);
  }
  // gap 1 - min 16 bytes in fm, min 32 bytes in mfm
  trackloadingpointer = add_bytes(last, mfm ? 0x4E : 0xFF, mfm ? 50 : 26);
  check_consistency(toc);
 addfromqueue:
  DI();
  if (trackloadingqueuehead - trackloadingqueuetail) {
    EI();
    trackloadingpointer = add_sector(trackloadingpointer, *trackloadingqueue[trackloadingqueuetail], 0);
    trackloadingsectorsleft--;
    trackloadingqueuetail++;
    check_consistency(toc);
    goto addfromqueue;
  } else {
    trackloadingready = true;
    EI();
  }
  while (trackloadingsectorsleft) {
    // TODO: something in the meantime
    maintain_track_storage();
  }
  check_consistency(toc);
  // make sure we actually put some stuff in the thing
  bpassert(bytetotal > (mfm ? 4000 : 2000));
  // gap 4
  int gapsize = (mfm ? 6250 : 3125) - bytetotal;
  bpassert(gapsize >= 0);
  while (gapsize > 768) {
    trackloadingpointer = add_bytes(trackloadingpointer, mfm ? 0x4E : 0xFF, 768);
    gapsize -= 768;
  }
  if (gapsize) {
    trackloadingpointer = add_bytes(trackloadingpointer, mfm ? 0x4E : 0xFF, gapsize);
  }
  bpassert((mfm ? 6250 : 3125) == bytetotal);
  ((struct bytes *)trackloadingpointer)->cdr = toc;
  fill_toc(toc);
  return toc;
}

// rampages through the track and frees the blocks
static void free_track(struct toc *start) {
  void *last = start->cdr;
  void *next;
  start->cdr = NULL;
  while (last) {
    next = ((struct bytes *)last)->cdr;
    free_block(last);
    last = next;
  }
}

static uint_fast8_t headroom(uint_fast8_t trackcache[6], unsigned int candidate, bool fudge) {
  uint_fast8_t candidatetrack = trackcache[candidate + 2];
  uint_fast8_t candidatecurrent = trackcache[candidate & 0b1];
  if (candidate & 0b10) {
    // last track
    if (candidatetrack >= 39 && fudge) {
      return 40;
    }
    return candidatetrack - candidatecurrent;
  } else {
    // first track
    if (candidatetrack == 0 && fudge) {
      return 40;
    }
    return candidatecurrent - candidatetrack;
  }
}

// this function should only be called while in thread mode
// BEWARE: could experience an integer wrapping bug if the "head"
//         is stepped outside of the loaded range, but by that
//         point we're already stuffed
void maintain_tracks(void) {
    DI();
    if (selecteddrive) {
      bpassert(selecteddrive->selected);
    }
    EI();
  // make sure that we have drives
  bpassert(drive1.enabled || drive2.enabled);
  // so we don't have to keep reading volatiles from memory
  uint_fast8_t trackcache[6];
  trackcache[0] = drive1.currenttrack;
  trackcache[1] = drive2.currenttrack;
  trackcache[2] = drive1.firsttrack;
  trackcache[3] = drive2.firsttrack;
  trackcache[4] = drive1.lasttrack;
  trackcache[5] = drive2.lasttrack;
  // m-m-max headroom
  uint_fast8_t maxheadroom = 0;
  uint_fast8_t minheadroom = 40;
  for (unsigned int i = 0; i < 4; i++) {
    if (!((i & 0b1) ? drive2.enabled : drive1.enabled)) {
      continue;
    }
    if (headroom(trackcache, i, false) > maxheadroom) {
      maxheadroom = headroom(trackcache, i, false);
    } else if (headroom(trackcache, i, true) < minheadroom) {
      minheadroom = headroom(trackcache, i, true);
    }
  }
    DI();
    if (selecteddrive) {
      bpassert(selecteddrive->selected);
    }
    EI();
  // unload (if low on free memory) and load tracks
  if (minheadroom < maxheadroom - 1) {
    while (freetrackstorage.bigcount + freetrackstorage.newbigcount < MIN_FREE_BIGBLOCKS) {
      unsigned int candidate = 4;
      trackcache[0] = drive1.currenttrack;
      trackcache[1] = drive2.currenttrack;
      maxheadroom = 0;
      for (unsigned int i = 0; i < 4; i++) {
	if (!((i & 0b1) ? drive2.enabled : drive1.enabled)) {
	  continue;
	}
	if (headroom(trackcache, i, false) > maxheadroom) {
	  candidate = i;
	  maxheadroom = headroom(trackcache, i, false);
	}
      }
      bpassert(maxheadroom);
      bpassert(candidate < 4);
      switch (candidate) {
      case 0:
	free_track(drive1.tracks[trackcache[2]]);
	trackcache[2]++;
	break;
      case 1:
	free_track(drive2.tracks[trackcache[3]]);
	trackcache[3]++;
	break;
      case 2:
	free_track(drive1.tracks[trackcache[4]]);
	trackcache[4]--;
	break;
      case 3:
	free_track(drive2.tracks[trackcache[5]]);
	trackcache[5]--;
	break;
      }
    }
    sort_big_blocks();
    sort_medium_blocks();
    sort_small_blocks();
  }
    DI();
    if (selecteddrive) {
      bpassert(selecteddrive->selected);
    }
    EI();
  while (freetrackstorage.bigcount > MIN_FREE_BIGBLOCKS) {
    unsigned int candidate = 4;
    trackcache[0] = drive1.currenttrack;
    trackcache[1] = drive2.currenttrack;
    if (drive1.enabled && !drive1.loaded) {
      // the drive doesn't have anything loaded
      candidate = 0;
      drive1.loaded = true;
      goto loadtrack;
    } else if (drive2.enabled && !drive2.loaded) {
      // this one doesn't have anything loaded
      candidate = 1;
      drive2.loaded = true;
      goto loadtrack;
    }
    minheadroom = 40;
    for (unsigned int i = 0; i < 4; i++) {
      if (!((i & 0b1) ? drive2.enabled : drive1.enabled)) {
	continue;
      }
      if (headroom(trackcache, i, true) < minheadroom) {
	candidate = i;
	minheadroom = headroom(trackcache, i, true);
      }
    }
    if (candidate == 4) {
      // all tracks are loaded
      break;
    }
    // this gets skipped if the disks aren't loaded
    if (candidate > 1) {
      trackcache[candidate + 2]++;
    } else {
      trackcache[candidate + 2]--;
    }
  loadtrack:
    union trackrequest request;
    volatile struct disk *thisdisk = (candidate & 1) ? &drive2 : &drive1;
    request.diskid = thisdisk->diskid;
    request.side1 = false;
    request.sectorcount = thisdisk->sectorspertrack;
    request.trackno = trackcache[candidate + 2];
    thisdisk->tracks[request.trackno] = load_track(request, thisdisk->mfm);
  }
    DI();
    if (selecteddrive) {
      bpassert(selecteddrive->selected);
    }
    EI();
    DI();
  // flush cache
  drive1.firsttrack = trackcache[2];
  drive2.firsttrack = trackcache[3];
  drive1.lasttrack = trackcache[4];
  drive2.lasttrack = trackcache[5];
  EI();
    DI();
    if (selecteddrive) {
      bpassert(selecteddrive->selected);
    }
    EI();
}
