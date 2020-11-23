// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"
void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.
struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  int n;
} kmem[NCPU];


void
kinit()
{
  for(int i=0;i<NCPU;i++){
	char name[12];
	snprintf(name,12,"kmem_lock%d",i);
  	initlock(&kmem[i].lock,name);
	kmem[i].n=0;
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  push_off();
  int id=cpuid();
  struct run *r;
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP){
  	pop_off();
    panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);
  r = (struct run*)pa;
  // Free Main Process
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  kmem[id].n++;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int id=cpuid();
  struct run *r;

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r){
    kmem[id].freelist = r->next;
	kmem[id].n--;
  	release(&kmem[id].lock);
  }
  else{
  	release(&kmem[id].lock);
  	//Steal the most CPU
	for(int i=(id+1)%NCPU;i!=id;i=(i+1)%(NCPU)){
		acquire(&kmem[i].lock);
		if(kmem[i].n>0){
			r=kmem[i].freelist;
			kmem[i].freelist=r->next;
			kmem[i].n--;
			release(&kmem[i].lock);
			break;
		}
		release(&kmem[i].lock);
	}
  }

  pop_off();
  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  
  return (void*)r;
}
