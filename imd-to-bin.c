#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include <stdbool.h>
#include <stdint.h>

#define BUFFER_LENGTH 1024

#define CRC_POLYNOMIAL 0x1021 // CRC-16-CCITT

#define MAX_SECTORS 10


// this is the so-called "bad" crc
// https://srecord.sourceforge.net/crc16-ccitt.html
static inline uint32_t software_crc_step(uint32_t buffer, uint8_t byte) {
  buffer ^= byte << 16;
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
  return buffer >> 8;
}


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

int main(int argc, char **argv) {
  int imdfd;
  int binfd;
  struct sector *sectors;
  struct stat statbuf;
  char *imd;
  char *imdpointer;
  bool fail;
  size_t binfilesize;
  if (argc < 3) {
    printf("too few arguments\n");
    return 1;
  }
  imdfd = open(argv[1], O_RDONLY);
  if (imdfd == -1) {
    perror("Failed to open imd file");
    goto imdfail;
  }
  binfd = open(argv[2], O_CREAT|O_RDWR, 0660);
  if (binfd == -1) {
    perror("Failed to open bin file");
    goto binfail;
  }
  if (ftruncate(binfd, 40 * MAX_SECTORS * sizeof(struct sector)) == -1) {
    perror("Failed to grow bin file");
    goto resizefail;
  }
  sectors = mmap(NULL, 40 * MAX_SECTORS * sizeof(struct sector), PROT_WRITE|PROT_READ, MAP_SHARED, binfd, 0);
  if (sectors == MAP_FAILED) {
    perror("Failed to mmap bin file");
    goto sectorsfail;
  }
  if (fstat(imdfd, &statbuf) == -1) {
    perror("Failed to stat imd file");
    goto statfail;
  }
  imd = mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, imdfd, 0);
  if (imd == MAP_FAILED) {
    perror("Failed to mmap imd file");
    goto imdmapfail;
  }
  for (imdpointer = imd; *imdpointer != 0x1A; imdpointer++);
  imdpointer++;
  // now imdpointer points to the beginning of track 0
  {
    struct sector *sectorpointer = sectors;
    struct sector mysector;
    while (imdpointer < imd + statbuf.st_size) {
      // mode value
      bool mfm = *imdpointer > 2 ? true : false;
      imdpointer++;
      // cylinder
      uint_fast8_t track = *imdpointer;
      imdpointer++;
      // head
      bool side1 = *imdpointer & 0b1;
      if ((uint8_t)*imdpointer > 1) {
	printf("can't be bothered implementing sector cylinder or head maps\n");
	fail = true;
	break;
      }
      imdpointer++;
      // number of sectors in track
      int sectorcount = *imdpointer;
      if (sectorcount > MAX_SECTORS) {
	printf("problem: %i is too many sectors (max %u)\n", sectorcount, MAX_SECTORS);
	fail = true;
	break;
      } else if (sectorcount != 10) {
	printf("warning: cyl. %u has %i sectors instead of %u\n", track, sectorcount, MAX_SECTORS);
      }
      imdpointer++;
      // sector size
      uint_fast8_t length = *imdpointer;
      if (length > 3) {
	printf("problem: %i is too long sector length (max 3)\n", length);
	fail = true;
	break;
      }
      imdpointer++;
      // sector numbering map
      char sectornumbers[MAX_SECTORS];
      memcpy(sectornumbers, imdpointer, sectorcount);
      imdpointer += sectorcount;
      // presumably data records
      for (int i = 0; i < sectorcount; i++) {
	if (!(*imdpointer)) {
	  printf("stupid image missing cyl. %u log. sec. %u phy. sec. %u\n", track, sectornumbers[i], i);
	  fail = true;
	  break;
	}
	if (*imdpointer > 4) {
	  printf("warning: imd says read error cyl. %u log. sec. %u phy. sec. %u\n", track, sectornumbers[i], i);
	}
        mysector.mfm = mfm;
	mysector.side1 = side1;
	mysector.deleted = (*imdpointer - 1) & 0b010;
	mysector.sector = sectornumbers[i];
	mysector.track = track;
	mysector.length = length;
	memcpy(sectorpointer, &mysector, 4);
	if ((*imdpointer - 1) & 0b001) {
	  // compressed
	  imdpointer++;
	  memset(sectorpointer->data, *imdpointer, 128 << length);
	  imdpointer++;
	} else {
	  // not compressed
	  imdpointer++;
	  memcpy(sectorpointer->data, imdpointer, 128 << length);
	  imdpointer += 128 << length;
	}
	// we should really be computing crcs
	// based on precomputed AM crcs
	uint16_t crc;
	if (mysector.mfm) {
	  crc = mysector.deleted ? 0xD2F6 : 0xE295; // precomputed 2024-08-28
	} else {
	  crc = mysector.deleted ? 0x8FE7 : 0xBF84; // precomputed 2024-08-27
	}
	crc = software_crc(sectorpointer->data, 128 << length, crc);
	sectorpointer->crc[0] = crc >> 8;
	sectorpointer->crc[1] = crc & 0xFF;
	sectorpointer = (void *)sectorpointer + 4 + (128 << length);
      }
      if (fail) {
	break;
      }
    }
    binfilesize = (void *)sectorpointer - (void *)sectors;
  }
  munmap(imd, statbuf.st_size);
  munmap(sectors, 40 * MAX_SECTORS * sizeof(struct sector));
  if (ftruncate(binfd, binfilesize) == -1) {
    perror("Failed to shrink bin file");
  }
  close(binfd);
  close(imdfd);
  return fail;
 imdmapfail:
 statfail:
  munmap(sectors, 40 * MAX_SECTORS * sizeof(struct sector));
 sectorsfail:
 resizefail:
  close(binfd);
 binfail:
  close(imdfd);
 imdfail:
  return 1;
}
