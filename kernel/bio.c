// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13    // number of buckets
#define TABLE_SIZE NBUF

// hash table entry
struct entry {
  struct buf* buf_value;
};

struct spinlock buffer_lock[NBUCKET];      // a lock per bucket

struct entry table[NBUCKET][TABLE_SIZE];

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
} bcache;

static void
insert(struct buf* buf_value, struct entry (*p)[TABLE_SIZE], int posx, int posy)
{
  p[posx][posy].buf_value = buf_value;
  // p[posx][posy].buf_value->blockno = key;
}

static 
void put(int key, struct buf* buf_value)
{
  int i = key % NBUCKET;

  // is the key already present?
  int j = 0;
  for (; j < TABLE_SIZE; j++) {
    if ((!table[i][j].buf_value) || table[i][j].buf_value->blockno == key)
      break;
  }
  if(table[i][j].buf_value && table[i][j].buf_value->blockno == key) {
    // update the existing key.
    table[i][j].buf_value = buf_value;
  } else if(j != TABLE_SIZE) {
    // the new is new.
    insert(buf_value, table, i, j);
  } else {
    insert(buf_value, table, i, 0);
  }
}

static struct entry*
get(int key)
{
  int i = key % NBUCKET;

  struct entry* e = 0;
  int j = 0;
  for (; j < TABLE_SIZE; j++) {
    e = &table[i][j];
    if ((*e).buf_value && (*e).buf_value->blockno == key) {
      return e;
    }
  }

  return 0;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUCKET; ++i) {
    initlock(&buffer_lock[i], "bcache.bucket");
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; ++b) {
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  acquire(&buffer_lock[blockno % NBUCKET]);

  // When two processes concurrently miss in the cache, and need to find an unused block to replace. 
  // bcachetest test0 doesn't ever do this.
  struct entry* e = get(blockno);
  if (e) {
    if (e->buf_value->dev == dev) {
      e->buf_value->refcnt++;
      //release(&bcache.lock);  
      release(&buffer_lock[blockno % NBUCKET]);
      acquiresleep(&e->buf_value->lock);
      return e->buf_value;
    }
  }

  acquire(&bcache.lock);
  int min_tick_index = -1;
  uint min_ticks = -1;
  for (int i = 0; i < NBUF; ++i) {
    if (bcache.buf[i].refcnt == 0 && bcache.buf[i].ticks < min_ticks) {
      min_tick_index = i;
      min_ticks = bcache.buf[i].ticks;
    }
  }

  bcache.buf[min_tick_index].dev = dev;
  bcache.buf[min_tick_index].blockno = blockno;
  bcache.buf[min_tick_index].valid = 0;
  bcache.buf[min_tick_index].refcnt = 1;

  put(blockno, &bcache.buf[min_tick_index]);

  release(&bcache.lock);
  release(&buffer_lock[blockno % NBUCKET]);
  acquiresleep(&bcache.buf[min_tick_index].lock);
  return &bcache.buf[min_tick_index];
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  b->refcnt--;
  if (b->refcnt == 0) {
    acquire(&tickslock);
    b->ticks = ticks;
    release(&tickslock);
  }
}

void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}


