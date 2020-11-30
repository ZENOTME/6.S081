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

#define NBUCKET 13
struct node{
	struct buf* buf;
	struct node* next;
	struct node* prev;
};
struct entry{
	struct node head;
};

struct timer{
	uint blockno;
	struct timer* next;
	struct timer* prev;
};

struct {
  struct buf buf[NBUF];
  struct node node[NBUF];
  struct timer timer[NBUF];
  ///////////////////////////////
  struct entry entry[NBUCKET];
  struct spinlock entry_lock[NBUCKET];
  struct timer timer_head;
  struct spinlock lock;

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
} bcache;

void
binit(void)
{
  struct buf *b;
  struct node *no;
  for(int i=0;i<NBUCKET;i++){
  	initlock(&bcache.entry_lock[i], "bcache.bucket");
  }
  initlock(&bcache.lock,"bcache");
  // init buf_timer
  bcache.timer_head.prev=&bcache.timer_head;
  bcache.timer_head.next=&bcache.timer_head;
  for(int i=0;i<NBUF;i++){
  	bcache.timer[i].next=bcache.timer_head.next;
	bcache.timer[i].prev=&bcache.timer_head;
	bcache.timer_head.next->prev=&bcache.timer[i];
	bcache.timer_head.next=&bcache.timer[i];
  }
  // init entry 
  b=&bcache.buf[0];
  initsleeplock(&b->lock, "buffer");
  no=&bcache.node[0];
  no->buf=b;
  no->next=bcache.entry[0].head.next;
  no->prev=&bcache.entry[0].head;
  bcache.entry[0].head.next=no;

  for(int i=1;i<NBUF;i++){
	b=&bcache.buf[i];
    initsleeplock(&b->lock, "buffer");
	no=&bcache.node[i];
	no->buf=b;
	no->next=bcache.entry[0].head.next;
	no->prev=&bcache.entry[0].head;
	bcache.entry[0].head.next->prev=no;
    bcache.entry[0].head.next=no;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  struct node* no;
  struct timer* t;
  struct entry  e;
  uint id=blockno%NBUCKET;
  // Is the block already cached?
  acquire(&bcache.entry_lock[id]);
  e=bcache.entry[id];
  no=e.head.next;
  while(no!=(void*)0){
	b=no->buf;
  	if(b->dev == dev && b->blockno == blockno){
    	b->refcnt++;
     	release(&bcache.entry_lock[id]);
      	acquiresleep(&b->lock);
      	return b;
    }
	no=no->next;
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  release(&bcache.entry_lock[id]);
  acquire(&bcache.lock);

  for(t = bcache.timer_head.prev; t != &bcache.timer_head; t = t->prev){
    uint cp_id=t->blockno%NBUCKET;
	//research from entry
	acquire(&bcache.entry_lock[cp_id]);
  	e=bcache.entry[cp_id];
  	no=e.head.next;
  	while(no!=(void*)0){
		b=no->buf;
  		if(b->blockno==t->blockno){
    		if(b->refcnt==0){
				if(cp_id==id){
					//change buf
					b->dev=dev;
					b->blockno=blockno;
					b->valid=0;
					b->refcnt=1;
					//change timer
					t->blockno=blockno;
					//
					release(&bcache.entry_lock[cp_id]);
					release(&bcache.lock);
					acquiresleep(&b->lock);
					return b;
				}else{
					//move buf
  					acquire(&bcache.entry_lock[id]);
					b->dev=dev;
					b->blockno=blockno;
					b->valid=0;
					b->refcnt=1;
					no->prev->next=no->next;
//printf("%p\n",no->prev);
					if(no->next!=(void*)0)
						no->next->prev=no->prev;

					no->prev=&bcache.entry[id].head;
					no->next=bcache.entry[id].head.next;
					if(bcache.entry[id].head.next)
						bcache.entry[id].head.next->prev=no;
					bcache.entry[id].head.next=no;
					//change timer
					t->blockno=blockno;
					//
					release(&bcache.entry_lock[id]);
					release(&bcache.entry_lock[cp_id]);
					release(&bcache.lock);
					acquiresleep(&b->lock);
					return b;
				}
			}
    	}
		no=no->next;
  	}
	release(&bcache.entry_lock[cp_id]);
  }
  release(&bcache.lock);
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
  struct timer* t;
  uint blockno;
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  acquire(&bcache.entry_lock[b->blockno%NBUCKET]);
  b->refcnt--;
  if (b->refcnt == 0) {
    blockno=b->blockno;
	for(t=bcache.timer_head.prev;t->blockno!=blockno&&t!=&bcache.timer_head;t=t->prev){}
    // no one is waiting for it.
	if(t!=&bcache.timer_head){
   		t->next->prev = t->prev;
   		t->prev->next = t->next;
   		t->next = bcache.timer_head.next;
   		t->prev = &bcache.timer_head;
		bcache.timer_head.next->prev = t;
    	bcache.timer_head.next = t;
	}else{
		panic("brelease timer faild");
	}
  }
  release(&bcache.entry_lo-ck[b->blockno%NBUCKET]);
  release(&bcache.lock);
  
}

void
bpin(struct buf *b) {
  acquire(&bcache.entry_lock[b->blockno%NBUCKET]);
  b->refcnt++;
  release(&bcache.entry_lock[b->blockno%NBUCKET]);
}

void
bunpin(struct buf *b) {
  acquire(&bcache.entry_lock[b->blockno%NBUCKET]);
  b->refcnt--;
  release(&bcache.entry_lock[b->blockno%NBUCKET]);
}


