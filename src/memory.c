#include "memory.h"
#include "main.h"

#include <stddef.h>
#include <stdbool.h>

// TODO: don't bother sorting free blocks if we've just split them

union trackdata trackstorage[TRACKSTORAGE_LENGTH];
struct trackdatafreememory freetrackstorage;

void initialize_track_storage(void) {
  freetrackstorage.big = &(trackstorage[TRACKSTORAGE_LENGTH - 1].big);
  for (int i = TRACKSTORAGE_LENGTH - 1; i > 0; i--) {
    trackstorage[i].big.cdr = &(trackstorage[i-1]);
  }
  trackstorage[0].big.cdr = NULL;
  freetrackstorage.bigcount = TRACKSTORAGE_LENGTH;

  freetrackstorage.mediumcount = 0;
  freetrackstorage.smallcount = 0;
  freetrackstorage.newbigcount = 0;
  freetrackstorage.newmediumcount = 0;
  freetrackstorage.newsmallcount = 0;
}

static void split_big_track_storage_to_medium(void) {
  union trackdata *block = (union trackdata *)freetrackstorage.big;
  freetrackstorage.big = block->big.cdr;
  freetrackstorage.bigcount--;
  block->medium[0].cdr = freetrackstorage.newmedium;
  block->medium[1].cdr = &(block->medium[0]);
  block->medium[2].cdr = &(block->medium[1]);
  freetrackstorage.newmedium = &(block->medium[2]);
  freetrackstorage.newmediumcount += 3;
}

static void split_big_track_storage_to_small(void) {
  union trackdata *block = (union trackdata *)freetrackstorage.big;
  freetrackstorage.big = block->big.cdr;
  freetrackstorage.bigcount--;
  block->small[0].cdr = freetrackstorage.newsmall;
  for (int i = 1; i < 9; i++) {
    block->small[i].cdr =  &(block->small[i - 1]);
  }
  freetrackstorage.newsmall = &(block->small[8]);
  freetrackstorage.newsmallcount += 9;
}

// takes pile to merge into and pile to merge from,
// returns last element of the pile that it infers exists
// BEWARE: the behaviour of this function is undefined if *mainpile or pile1 are NULL
// BEWARE: assumes that *mainpile contains the smallest card on top
static void merge_piles(struct bytes **mainpile,
			struct bytes *pile1
			) {
  struct bytes *pile0 = *mainpile;
  struct bytes *oldcard;
  if (pile0 > pile1) {
    //*mainpile = pile0; // redundant
    goto pile0_last;
  } else {
    *mainpile = pile1;
    goto pile1_last;
  }
 pile0_last:
  oldcard = pile0;
  pile0 = pile0->cdr;
  if (pile0 > pile1) {
    // don't need to change its cdr
    // pile0 cannot run out here as that would imply that the top of *mainpile
    // was larger than some card in pile1
    goto pile0_last;
  } else {
    // do need to change its cdr
    // if pile0 had run out last time that would also imply that the top
    // of *mainpile was larger than some card in pile1
    oldcard->cdr = pile1;
    goto pile1_last;
  }
 pile1_last:
  oldcard = pile1;
  pile1 = pile1->cdr;
  if (pile0 > pile1) {
    // do need to change its cdr
    oldcard->cdr = pile0;
    // there's an instruction for this... idiots...
    // WHY IS THE PROCESSOR MISSING INSTRUCTIONS???
#if 1
    if (!pile1) {
      // pile1 has run out => pile0 contains the rest
      return;
    }
#else
    asm goto ("CBZ %[pile1], %l[exit]" : : [pile1] "r" (pile1) : : exit);
#endif
    goto pile0_last;
  } else {
    // don't need to change its cdr
    // pile1 must not have run out here as we have already established
    // that pile0 does not run out
    goto pile1_last;
  }
#if 0
  exit:
    return;
#endif
}

// takes the first element of the unsorted linked list,
// returns the first element of the sorted linked list
// note that this sorts in memory order, building down
// we build down so that NULL is at the end - we have to check less often?
// modified version of patience sort that is limited to 4 piles
// O(n) best case (sorted), O(n^2) worst case (reverse sorted)
// O(1) space complexity
// the linked list must not be mutated while this is running
/*
 * If we make the analogy that the element in the cdr of another element is
 * like the card on the face side of another card, then this is like picking
 * up cards from a face-down pile and placing them onto face-up piles, but only
 * if the value on the card is greater than that of the card on the top of the pile.
 * If this is not possible with the leftmost pile, we continue to the right, and if
 * there are no piles left we start a new one, but only if there are less than four
 * piles. If there are more we put our card aside and merge them all into the first
 * pile, before adding it to a new pile.
 */
static struct bytes * impatience_sort(struct bytes *list) {
  struct bytes *mainpilebottom = list;
  struct bytes *mainpiletop = list;
  struct bytes *pile1bottom, *pile2bottom, *pile3bottom;
  // the cdr of the top of a pile is not to be trusted
  struct bytes *pile1top, *pile2top, *pile3top;
  pile1bottom = pile2bottom = pile3bottom = NULL;
  //pile1top = pile2top = pile3top = NULL;
  struct bytes *card = list->cdr;
  // ik dijkstra will be rolling in his grave but this might be more readable than whiles?
 onepile:
  if (card < mainpiletop) {
    //mainpiletop->cdr = card; // might be safe to assume this is already the case
    // look, if we're here this list is already in order to this point,
    // i don't think i need to prove it to you
    if (!card) {
      goto onesorted;
    } else {
      mainpiletop = card;
      card = card->cdr;
      goto onepile;
    }
  } else {
    pile1bottom = card;
    pile1top = card;
    card = card->cdr;
    goto twopiles;
  }
 twopiles:
  if (card < mainpiletop) {
    mainpiletop->cdr = card;
    if (!card) {
      goto twosorted;
    } else {
      mainpiletop = card;
      card = card->cdr;
      goto twopiles;
    }
  } else if (card < pile1top) {
    pile1top->cdr = card;
    pile1top = card;
    card = card->cdr;
    goto twopiles;
  } else {
    pile2bottom = card;
    pile2top = card;
    card = card->cdr;
    goto threepiles;
  }
 threepiles:
  if (card < mainpiletop) {
    mainpiletop->cdr = card;
    if (!card) {
      goto threesorted;
    } else {
      mainpiletop = card;
      card = card->cdr;
      goto threepiles;
    }
  } else if (card < pile1top) {
    pile1top->cdr = card;
    pile1top = card;
    card = card->cdr;
    goto threepiles;
  } else if (card < pile2top) {
    pile2top->cdr = card;
    pile2top = card;
    card = card->cdr;
    goto threepiles;
  } else {
    pile3bottom = card;
    pile3top = card;
    card = card->cdr;
    goto fourpiles;
  }
 fourpiles:
  if (card < mainpiletop) {
    mainpiletop->cdr = card;
    if (!card) {
      goto foursorted;
    } else {
      mainpiletop = card;
      card = card->cdr;
      goto fourpiles;
    }
  } else if (card < pile1top) {
    pile1top->cdr = card;
    pile1top = card;
    card = card->cdr;
    goto fourpiles;
  } else if (card < pile2top) {
    pile2top->cdr = card;
    pile2top = card;
    card = card->cdr;
    goto fourpiles;
  } else if (card < pile3top) {
    pile3top->cdr = card;
    pile3top = card;
    card = card->cdr;
    goto fourpiles;
  } else {
    // crapola
    // this part gives it O(n^2) worst case time complexity - maybe?
    // the top of the main pile always has the smallest card yet encountered:
    // it must be in the main pile as we try to put things there first,
    // and it must be at the top because there's nothing smaller to put on top of it
    mainpiletop->cdr = pile1top->cdr = pile2top->cdr = pile3top->cdr = NULL;
    merge_piles(&mainpilebottom, pile1bottom);
    merge_piles(&pile2bottom, pile3bottom);
    merge_piles(&mainpilebottom, pile2bottom);
    // the top of the main pile must be the same because it is the smallest
    // the others are in an inconsistent state, but we won't use them without
    // resetting them first
    pile1top = card;
    pile1bottom = card;
    card = card->cdr;
    goto twopiles;
  }
 foursorted:
  pile3top->cdr = NULL;
  merge_piles(&pile2bottom, pile3bottom);
 threesorted:
  pile2top->cdr = NULL;
  merge_piles(&pile1bottom, pile2bottom); // this should be faster if mainpile is the biggest
 twosorted:
  pile1top->cdr = NULL;
  merge_piles(&mainpilebottom, pile1bottom);
 onesorted: // already sorted lmao
  return mainpilebottom;
}

// a normal merge for normal people
// BEWARE: assumes you're not an idiot and are actually merging something
static void merge(struct bytes **mainlist,
		  struct bytes *list1
		  ) {
  struct bytes *list0 = *mainlist;
  struct bytes *oldelement;
  if (list0 > list1) {
    goto list0_last;
  } else {
    *mainlist = list1;
    goto list1_last;
  }
 list0_last:
  // please check that neither list has run out before you jump here
  oldelement = list0;
  list0 = list0->cdr;
  if (list0 > list1) {
    // don't need to change its cdr
    // list0 has obviously not run out - it is higher than something
    goto list0_last;
  } else {
    // do need to change its cdr
    oldelement->cdr = list1;
    // need to check we didn't just exhaust list0
    if (!list0) {
      // list0 has run out => list1 contains the rest
      return;
    }
    goto list1_last;
  }
 list1_last:
  // please check that neither list has run out before you jump here
  oldelement = list1;
  list1 = list1->cdr;
  if (list0 > list1) {
    // do need to change its cdr
    oldelement->cdr = list0;
    if (!list1) {
      // list1 has run out => list0 contains the rest
      return;
    }
    goto list0_last;
  } else {
    // don't need to change its cdr
    // list1 mustn't have run out as it is greater than or equal to list0,
    // and we made sure that list0 is not NULL
    goto list1_last;
  }
}

static struct bytes * reverse(struct bytes *start) {
  struct bytes *prev = start;
  struct bytes *current = prev->cdr;
  struct bytes *next = current->cdr;
  start->cdr = NULL;
  while (next) {
    current->cdr = prev;
    prev = current;
    current = next;
    next = current->cdr;
  }
  return current;
}


// don't call this if there are no new blocks. that's stupid.
// so much easier to write this when not bothering with thread safety
// or premature optimisation
static inline void sort_new_blocks_into_regular_blocks(struct bytes **newblocks,
						       unsigned int *newblockscount,
						       struct bytes **regularblocks,
						       unsigned int *regularblockscount
						       ) {
  if (*newblockscount > 1) {
    *newblocks = impatience_sort(*newblocks);
  }
  if (!(*regularblockscount)) {
    *regularblocks = *newblocks;
    *regularblockscount = *newblockscount;
    goto finish;
  }
  merge(regularblocks, *newblocks);
  *regularblockscount += *newblockscount;
 finish:
  *newblocks = NULL;
  *newblockscount = 0;
  return;
}

// quirk: will not merge the first small block - this should not matter
// if it does matter, then you should think about why
// only call if you know newmedium is empty
static bool merge_small_blocks_to_medium_blocks(void) {
  struct smalldatablock *lastmedium = NULL;
  struct bytes *lastbytes = freetrackstorage.small;
  struct bytes *bytes0 = freetrackstorage.small->cdr;
  struct bytes *bytes1;
  struct bytes *bytes2;
 firstbytes:
  if (!bytes0) {
    goto end;
  }
  // these things are sorted in descending order so it's a bit weird
  if (((unsigned int)bytes0 - (unsigned int)(&trackstorage))
      % sizeof(struct smalldatablock)
      != sizeof(struct smalldatablock) - sizeof(struct bytes)) {
    bytes0 = bytes0->cdr;
    goto firstbytes;
  }
  bytes1 = bytes0->cdr;
  if ((unsigned int)bytes0 - (unsigned int)bytes1 - sizeof(struct bytes)) {
    if (!bytes1) {
      goto end;
    }
    bytes0 = bytes1->cdr;
    goto firstbytes;
  }
  bytes2 = bytes1->cdr;
  if ((unsigned int)bytes1 - (unsigned int)bytes2 - sizeof(struct bytes)) {
    if (!bytes2) {
      goto end;
    }
    bytes0 = bytes2->cdr;
    goto firstbytes;
  }
  // success!
  bpassert(((unsigned int)bytes2 - (unsigned int)(&trackstorage))
	   % sizeof(struct smalldatablock)
	   == 0);
  struct smalldatablock *newmedium = (struct smalldatablock *)bytes2;
  lastmedium->cdr = newmedium;
  newmedium = lastmedium;
  lastbytes->cdr = bytes2->cdr;
  freetrackstorage.newmediumcount++;
  freetrackstorage.smallcount -= 3;
  bytes0 = bytes2->cdr;
  goto firstbytes;
 end:
  if (lastmedium) {
    lastmedium->cdr = NULL;
    return true;
  }
  return false;
}

// quirk: will not merge the first medium block - this should not matter
// if it does matter, then you should think about why
// only call if you know newbig is empty
static bool merge_medium_blocks_to_big_blocks(void) {
  struct datablock *lastbig = NULL;
  struct smalldatablock *lastsmalldatablock = freetrackstorage.medium;
  struct smalldatablock *smalldatablock0 = freetrackstorage.medium->cdr;
  struct smalldatablock *smalldatablock1;
  struct smalldatablock *smalldatablock2;
 firstsmalldatablock:
  if (!smalldatablock0) {
    goto end;
  }
  // these things are sorted in descending order so it's a bit weird
  if (((unsigned int)smalldatablock0 - (unsigned int)(&trackstorage))
      % sizeof(struct datablock)
      != sizeof(struct datablock) - sizeof(struct smalldatablock)) {
    smalldatablock0 = smalldatablock0->cdr;
    goto firstsmalldatablock;
  }
  smalldatablock1 = smalldatablock0->cdr;
  if ((unsigned int)smalldatablock0 - (unsigned int)smalldatablock1 - sizeof(struct smalldatablock)) {
    if (!smalldatablock1) {
      goto end;
    }
    smalldatablock0 = smalldatablock1->cdr;
    goto firstsmalldatablock;
  }
  smalldatablock2 = smalldatablock1->cdr;
  if ((unsigned int)smalldatablock1 - (unsigned int)smalldatablock2 - sizeof(struct smalldatablock)) {
    if (!smalldatablock2) {
      goto end;
    }
    smalldatablock0 = smalldatablock2->cdr;
    goto firstsmalldatablock;
  }
  // success!
  bpassert(((unsigned int)smalldatablock2 - (unsigned int)(&trackstorage))
	   % sizeof(struct datablock)
	   == 0);
  struct datablock *newbig = (struct datablock *)smalldatablock2;
  lastbig->cdr = newbig;
  lastbig = newbig;
  lastsmalldatablock->cdr = smalldatablock2->cdr;
  freetrackstorage.newbigcount++;
  freetrackstorage.mediumcount -= 3;
  smalldatablock0 = smalldatablock2->cdr;
  goto firstsmalldatablock;
 end:
  if (lastbig) {
    lastbig->cdr = NULL;
    return true;
  }
  return false;
}

bool sort_big_blocks(void) {
  if (freetrackstorage.newbigcount) {
    sort_new_blocks_into_regular_blocks((struct bytes **)&(freetrackstorage.newbig),
					&(freetrackstorage.newbigcount),
					(struct bytes **)&(freetrackstorage.big),
					&(freetrackstorage.bigcount)
					);
    return true;
  }
  return false;
}

bool sort_medium_blocks(void) {
  if (freetrackstorage.newmediumcount) {
    sort_new_blocks_into_regular_blocks((struct bytes **)&(freetrackstorage.newmedium),
					&(freetrackstorage.newmediumcount),
					(struct bytes **)&(freetrackstorage.medium),
					&(freetrackstorage.mediumcount)
					);
    freetrackstorage.mergemedium = true;
    return true;
  }
  return false;
}

bool sort_small_blocks(void) {
  if (freetrackstorage.newsmallcount) {
    sort_new_blocks_into_regular_blocks(&(freetrackstorage.newsmall),
					&(freetrackstorage.newsmallcount),
					&(freetrackstorage.small),
					&(freetrackstorage.smallcount)
					);
    freetrackstorage.mergesmall = true;
    return true;
  }
  return false;
}

// this will need to sort the new blocks and to merge and split blocks
// TODO: remember when attempting to merge blocks failed
void maintain_track_storage(void) {
  static uint_fast8_t stage;
  static bool mergesmall;
  static bool mergemedium;
  if (stage == 3) {
    stage = 0;
  }
  switch (stage) {
  case 0:
    if (sort_big_blocks()) {
      break;
    }
    if ((freetrackstorage.mediumcount > TRACKSTORAGE_MAX_MEDIUM) && freetrackstorage.mergemedium) {
      merge_medium_blocks_to_big_blocks();
      // we know that the medium blocks were sorted, so our new bigs should be too
      if (freetrackstorage.newbig) {
	merge((struct bytes **)&(freetrackstorage.big), (struct bytes *)freetrackstorage.newbig);
      } else {
	freetrackstorage.mergemedium = false;
      }
      break;
    }
    stage++;
  case 1:
    if (sort_medium_blocks()) {
      break;
    }
    if ((freetrackstorage.smallcount > TRACKSTORAGE_MAX_SMALL) && freetrackstorage.mergesmall) {
      merge_small_blocks_to_medium_blocks();
      // we know that the small blocks were sorted, so our new mediums should be too
      if (freetrackstorage.newmedium) {
	merge((struct bytes **)&(freetrackstorage.medium), (struct bytes *)freetrackstorage.newmedium);
      } else {
	freetrackstorage.mergesmall = false;
      }
      break;
    }
    if (freetrackstorage.mediumcount < TRACKSTORAGE_MIN_MEDIUM) {
      for (unsigned int i = freetrackstorage.mediumcount; i < TRACKSTORAGE_MIN_MEDIUM; i += 3) {
	split_big_track_storage_to_medium();
      }
      // the big blocks were sorted, so the medium ones should be if the function is correct
      // honestly i can't be bothered so we sort them in
      sort_medium_blocks();
      break;
    }
    stage++;
  case 2:
    if (sort_small_blocks()) {
      break;
    }
    if (freetrackstorage.smallcount < TRACKSTORAGE_MIN_SMALL) {
      for (unsigned int i = freetrackstorage.smallcount; i < TRACKSTORAGE_MIN_SMALL; i += 9) {
	split_big_track_storage_to_small();
      }
      // the big blocks were sorted, so the small ones should be if the function is correct
      // honestly i can't be bothered so we sort them in
      sort_small_blocks();
      break;
    }
    break;
  }
  stage++;
}

void * alloc(enum type type) {
  void *rv;
  bpassert(freetrackstorage.bigcount);
  switch (type) {
  case FREEBIG:
  case TOC:
  case DATABLOCK:
    freetrackstorage.bigcount--;
    rv = freetrackstorage.big;
    freetrackstorage.big = ((struct bytes *)rv)->cdr;
    return rv;
  case FREEMEDIUM:
  case SMALLDATABLOCK:
    if (!freetrackstorage.mediumcount) {
      split_big_track_storage_to_medium();
      sort_medium_blocks();
    }
    freetrackstorage.mediumcount--;
    rv = freetrackstorage.medium;
    freetrackstorage.medium = ((struct bytes *)rv)->cdr;
    return rv;
  case FREESMALL:
  case TINYDATABLOCK:
  case BYTES:
  case FMAM:
  case MFMAM:
    if (!freetrackstorage.smallcount) {
      split_big_track_storage_to_small();
      sort_small_blocks();
    }
    freetrackstorage.smallcount--;
    rv = freetrackstorage.small;
    freetrackstorage.small = ((struct bytes *)rv)->cdr;
    return rv;
  default:
    bpassert(false);
  }
}

void free_block(void *block) {
  // im such a rebel i add things to the start of linked lists instead of the end
  // n.b. this is actually normal in lisp
  // also we don't bother to reset the type
  switch (((struct bytes *)block)->type) {
  case FREEBIG:
  case TOC:
  case DATABLOCK:
    ((struct datablock *)block)->cdr = freetrackstorage.newbig;
    freetrackstorage.newbig = block;
    freetrackstorage.newbigcount++;
    break;
  case FREEMEDIUM:
  case SMALLDATABLOCK:
    ((struct smalldatablock *)block)->cdr = freetrackstorage.newmedium;
    freetrackstorage.newmedium = block;
    freetrackstorage.newmediumcount++;
    break;
  case FREESMALL:
  case TINYDATABLOCK:
  case BYTES:
  case FMAM:
  case MFMAM:
    ((struct tinydatablock *)block)->cdr = freetrackstorage.newsmall;
    freetrackstorage.newsmall = block;
    freetrackstorage.newsmallcount++;
    break;
  default:
    break;
  }
}
