// Buffer cache.

// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.

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

#define NBUCKET 17
struct {
  struct spinlock evict_lk;
  struct buf buf[NBUF];

  struct buf bucket[NBUCKET];
  struct spinlock bucket_lk[NBUCKET];
} bcache;

void
binit(void)
{
  struct buf *b;

  for(int i = 0; i < NBUCKET; ++i){
    initlock(&bcache.bucket_lk[i], "bcache");
    bcache.bucket[i].next = &bcache.bucket[i];
    bcache.bucket[i].prev = &bcache.bucket[i];
  }
    
  initlock(&bcache.evict_lk, "bcache");

  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->prev = &bcache.bucket[0];
    b->next = bcache.bucket[0].next;
    bcache.bucket[0].next->prev = b;
    bcache.bucket[0].next = b;

    b->refcnt = 0;
    b->timestamp = 0;
    initsleeplock(&b->lock, "buffer");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket_id = blockno % NBUCKET;

  acquire(&bcache.bucket_lk[bucket_id]);
  // Is the block already cached?
  for(b = bcache.bucket[bucket_id].next; b != &bcache.bucket[bucket_id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bucket_lk[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  release(&bcache.bucket_lk[bucket_id]);

  // Not cached. Get evict lock and check again
  acquire(&bcache.evict_lk);

  //acquire(&bcache.bucket_lk[bucket_id]); //Reduce the granularity of locks
  for(b = bcache.bucket[bucket_id].next; b != &bcache.bucket[bucket_id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bucket_lk[bucket_id]);
      b->refcnt++;
      release(&bcache.bucket_lk[bucket_id]);
      release(&bcache.evict_lk);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  struct buf* evict = 0;
  int change = 0, evict_bucket_id = -1;
  // Recycle the least recently used (LRU) unused buffer.
  for(int i = 0; i < NBUCKET; ++i){
    if(i == bucket_id) 
      continue;
    
    acquire(&bcache.bucket_lk[i]);
    for(b = bcache.bucket[i].next; b != &bcache.bucket[i]; b = b->next){
      if(b->refcnt == 0 && (evict == 0 || b->timestamp < evict->timestamp)){

        if(evict_bucket_id != -1 && evict_bucket_id != i){
          release(&bcache.bucket_lk[evict_bucket_id]);   
        }

        evict_bucket_id = i;
        evict = b;
        change = 1;
      }
    }
    if(!change)
      release(&bcache.bucket_lk[i]);
    change = 0;
  }

  if(evict){
      acquire(&bcache.bucket_lk[bucket_id]);

      evict->dev = dev;
      evict->blockno = blockno;
      evict->valid = 0;
      evict->refcnt = 1;

      evict->prev->next = evict->next;
      evict->next->prev = evict->prev;
      evict->prev = &bcache.bucket[bucket_id];
      evict->next = bcache.bucket[bucket_id].next;
      bcache.bucket[bucket_id].next->prev = evict;
      bcache.bucket[bucket_id].next = evict;

      release(&bcache.bucket_lk[bucket_id]);
      release(&bcache.bucket_lk[evict_bucket_id]);
      release(&bcache.evict_lk);
      acquiresleep(&evict->lock);
      return evict;
  }
  
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

  int bucket_id = b->blockno % NBUCKET;

  acquire(&bcache.bucket_lk[bucket_id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    b->timestamp = ticks;
  }
  release(&bcache.bucket_lk[bucket_id]);
}

void
bpin(struct buf *b) {
  int bucket_id = b->blockno % NBUCKET;

  acquire(&bcache.bucket_lk[bucket_id]);
  b->refcnt++;
  release(&bcache.bucket_lk[bucket_id]);
}

void
bunpin(struct buf *b) {
  int bucket_id = b->blockno % NBUCKET;

  acquire(&bcache.bucket_lk[bucket_id]);
  b->refcnt--;
  release(&bcache.bucket_lk[bucket_id]);
}

