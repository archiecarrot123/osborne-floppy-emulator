#pragma once
#include <stdint.h>
#include <stdbool.h>

// i give up on thread safety - it takes too long and we don't need it
//#define DI() asm volatile ("CPSID i")
//#define EI() asm volatile ("CPSIE i")
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

enum type {
  // these free types will probably go unused
  FREEBIG,
  FREEMEDIUM,
  FREESMALL,
  TOC,
  DATABLOCK,
  SMALLDATABLOCK,
  TINYDATABLOCK,
  BYTES,
  FMAM,
  MFMAM
};

// blocks of different sizes
struct datablock {
  void *cdr;
  unsigned char type;
  unsigned char data[67];
};

struct smalldatablock {
  void *cdr;
  unsigned char type;
  unsigned char data[19];
};

struct tinydatablock {
  void *cdr;
  unsigned char type;
  unsigned char data[3];
};

// minimum value of count is 3
// this is because i'm lazy
// this also means that there is no way to represent less than 3 bytes
struct bytes {
  void *cdr;
  unsigned char type;
  unsigned char byte;
  uint16_t count;
};

// FMAM: rawbyte alone
// MFMAM: rawbyte repeated 3 times followed by byte3
// these should work in FM and MFM both, the names are just from the fact that
// MFMAM is based off of the address mark in the MFM IBM format
struct am {
  void *cdr;
  unsigned char type;
  unsigned char byte3;
  uint16_t rawbyte;
};

// different stuff
// the time is the byte number
struct toc {
  void *cdr;
  unsigned char type;
  unsigned char unused[3];
  uint16_t addresses[16];
  uint16_t times[16];
};

// linked list containing one track's data
// each trackdata is 72 bytes
union trackdata {
  struct datablock big;
  struct smalldatablock medium[3];
  struct bytes small[9];
};

// a lot of storage - this is more than an FM disk... all in memory...
#define TRACKSTORAGE_LENGTH 3072

// need some way to remember the free memory
struct trackdatafreememory {
  // sorted linked lists
  struct datablock *big;
  struct smalldatablock *medium;
  struct bytes *small;
  // unsorted linked lists
  struct datablock *newbig;
  struct smalldatablock *newmedium;
  struct bytes *newsmall;
  // amounts of free memory
  unsigned int bigcount;
  unsigned int mediumcount;
  unsigned int smallcount;
  unsigned int newbigcount;
  unsigned int newmediumcount;
  unsigned int newsmallcount;
  // whether or not we should try to merge them
  bool mergemedium;
  bool mergesmall;
};

// these are just guesses
#define TRACKSTORAGE_MAX_SMALL  100
#define TRACKSTORAGE_MAX_MEDIUM 100
#define TRACKSTORAGE_MIN_SMALL  10
#define TRACKSTORAGE_MIN_MEDIUM 10
#define TRACKSTORAGE_MIN_BIG    100

// important variables
extern union trackdata trackstorage[TRACKSTORAGE_LENGTH];
extern struct trackdatafreememory freetrackstorage;

// important functions
void initialize_track_storage(void);
void maintain_track_storage(void);
// all the other ones have the static attribute to tell the compiler
// that we're not using them in any other files - if you want to use them
// somewhere else just remove it and prototype them here

void * alloc(enum type type);
void free_block(void *block);

bool sort_big_blocks(void);
bool sort_medium_blocks(void);
bool sort_small_blocks(void);
