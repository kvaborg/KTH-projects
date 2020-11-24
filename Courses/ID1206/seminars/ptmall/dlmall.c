#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

#define _GNU_SOURCE

#define TRUE 1
#define FALSE 0

#define HEAD (sizeof(struct head))
#define MIN(size) (((size) > (8))?(size):(8))
#define LIMIT(size) (MIN(0) + HEAD + size)
#define MAGIC(memory) ((struct head*)memory - 1)
#define HIDE(block) (void *)((struct head*)block + 1)
#define ALIGN 8
#define ARENA (64 * 1024)

struct head {
  uint16_t bfree;
  uint16_t bsize;
  uint16_t free;    
  uint16_t size;
  struct head *next;
  struct head *prev;
};

struct head *after(struct head *block) {
  return (struct head*)((char *)block + HEAD + block->size);
}

struct head *before(struct head *block) {
  return (struct head*)((char *)block - (block->bsize + HEAD));
}

/*  */
struct head *split(struct head *block, int size) {
  int rsize = block->size - (size + HEAD);
  block->size = rsize;

  struct head *splt = after(block);
  splt->bsize = block->size;
  splt->bfree = block->free; // FALSE?
  splt->size = size;
  splt->free = FALSE;

  struct head *aft = after(splt);
  aft->bsize  = splt->size;

  return splt;
}

struct head *arena = NULL;

struct head *new_arena() {
  if (arena != NULL) {
    printf("one arena already allocated \n");
    return NULL;
  }

  // using mmap, but could have used sbrk
  struct head *new = mmap(NULL, ARENA, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

  if (new == MAP_FAILED) {
    printf("mmap failed: error %d\n", errno);
    return NULL;
  }

  /* make room for head and dummy */
  unsigned int size = ARENA - 2 * HEAD;

  new->bfree = FALSE;
  new->bsize = 0;
  new->free = TRUE;
  new->size = size;

  struct head *sentinel = after(new);

  /* only touch the status fields */
  sentinel->bfree = new->free;
  sentinel->bsize = new->size;
  sentinel->free = FALSE;
  sentinel->size = 0;

  /* this is the only arena we have */
  arena = (struct head*)new;

  return new;
}

/* create free list */
struct head *flist = NULL;

/* detach block from freelist. Update pointers of next and prev */
void detach(struct head *block) {
  if (block->next != NULL) {
    block->next->prev = block->prev;
  } 
  if (block->prev != NULL) {
    block->prev->next = block->next;
  } else {
    flist = block->next;
  }
}

void insert(struct head *block) {
  block->next = flist;
  block->prev = NULL;

  /* if the freelist is NOT empty, insert block first in list and update refs
   * and values of flist
  */
  if(flist != NULL) {
    flist->prev = block;  
  }

  flist = block;
}

int adjust(size_t request) {

  /* if request < 8 -> request = 8 */
  int newRequestSize = MIN(request);

  /* Adjust request in order to fit with ALIGN */
  if (newRequestSize % ALIGN == 0) {
    return newRequestSize;
  } else {
    int adj = newRequestSize % ALIGN;
    return newRequestSize + adj;
  }

}

/* find a block that fits the requested size */
struct head *find(int size) {
  if (flist == NULL) {
    printf("FLIST EMPTY\n");
    return NULL;
  }

  /* the order of procedure follows the algorithm:
   * - Go through freelist and find suitable block. If found -> detach
   * - Can we split the found block in two? If yes -> split and insert
   *   remaining back into freelist
   * - Mark found block as taken and update props + props of block after
   * - return pointer to beginning of data segment i.e. by hiding header
  */  
  struct head *freelist = flist;
  //printf("BEFORE WHILE\n");
  while (freelist) {
    //printf("ENTERING WHILE\n");
    if (freelist->size >= size) {
      //printf("ENTERING freelist->size >= size\n");
      detach(freelist);
      if (freelist->size >= LIMIT(size)) {
        //printf("lets split the block!\n");
        struct head *block = split(freelist, size);
        struct head *aft = after(block);
        aft->bfree = FALSE;      
        aft->bsize = block->size; 
        insert(freelist);
        return block;
      } else {
        freelist->free = FALSE;
        struct head *aft = after(freelist);
        aft->bfree = freelist->free;
        return freelist;
      }
    } else {
      freelist = freelist->next;
    }
  }

  //printf("lets return NULL\n");
  return NULL;
}

void *dalloc(size_t request) {
  if (request <= 0) {
    return NULL;
  }
  int size = adjust(request);
  struct head *taken = find(size);
  if (taken == NULL) {
    return NULL;
  } else {
    return HIDE(taken);
  }
}

void dfree(void *memory) {
  if (memory != NULL) {
    //printf("adress before magic: %p\n", memory);
    struct head *block = MAGIC(memory);
    //printf("memory that will be freed: %p\n", block);

    struct head *aft = after(block);
    block->free = TRUE;
    aft->bfree = block->free;
    insert(block);
  }
  return;
}

void init() {
  struct head *newArena = new_arena();
  insert(arena);
}

//  uint16_t bfree;
//  uint16_t bsize;
//  uint16_t free;    
//  uint16_t size;
//  struct head *next;
//  struct head *prev;
void sanity() {
  struct head *freelist = flist;
  struct head *are = arena;

  printf("FREELIST\n");
  while (freelist != NULL) {
    printf("Addr: %p, size: %d, free: %i, bsize: %d, bfree: %i next: %p, prev: %p\n",
        freelist, freelist->size, freelist->free, freelist->bsize, freelist->bfree, freelist->next, freelist->prev);
    if (freelist->free != TRUE) {
      printf("ERROR: block in freelist is not set to free");
      exit(1);
    } 
    freelist = freelist->next;
  }

  printf("ARENA\n");
  while (are->size != 0) {
    printf("Addr: %p, size: %d, free: %i, bsize: %d, bfree: %i next: (%p), prev: (%p)\n",
        are, are->size, are->free, are->bsize, are->bfree, are->next, are->prev);
    are = after(are);
  }

  return;
}

/* return the lenth of the free-list, only for statistics */
int length_of_free() {
  int i = 0;
  struct head *freelist  = flist;
  while(freelist != NULL) {
    i++;
    freelist = freelist->next;
  }
  return i; 
}

/* collect the sizes of all chunks in a buffer */
void sizes(int *buffer, int max) {
  struct head *freelist = flist;
  int i = 0;

  while((freelist != NULL) & (i < max)) {
    buffer[i] = freelist->size;
    i++;
    freelist = freelist->next;
  }
}