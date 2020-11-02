#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap((uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

/*
 * return a clone version of kernel page bale 
 */
pagetable_t
kvmclone()
{ 
  pagetable_t pagetable = (pagetable_t) kalloc();
  memset(pagetable, 0, PGSIZE);

  // uart registers
  if(mappages(pagetable, UART0, PGSIZE, UART0, PTE_R|PTE_W) != 0)
    panic("kvmclone:kvmmap");

  if(mappages(pagetable, VIRTIO0, PGSIZE, VIRTIO0, PTE_R|PTE_W) != 0)
    panic("kvmclone:kvmmap");

  //if(mappages(pagetable, CLINT, 0X10000, CLINT, PTE_R|PTE_W) != 0)
  //  panic("kvmmap");

  // PLIC
  if(mappages(pagetable, PLIC, 0X400000, PLIC, PTE_R|PTE_W) != 0)
    panic("kvmclone:kvmmap");

  // map kernel text executable and read-only.
  if(mappages(pagetable, KERNBASE, (uint64)etext-KERNBASE, KERNBASE, PTE_R|PTE_X) != 0)
    panic("kvmclone:kvmmap");

  // map kernel data and the physical RAM we'll make use of.
  if(mappages(pagetable, (uint64)etext, PHYSTOP-(uint64)etext, (uint64)etext, PTE_R|PTE_W) != 0)
    panic("kvmclone:kvmmap");

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R|PTE_X) != 0)
    panic("kvmclone:kvmmap");

  return pagetable;
}



// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0){
        return 0;
      }
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kernel_pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(kernel_pagetable, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}
// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// Walk free
int
uvmkunmap(pagetable_t pagetable, uint64 va,int dn){
  if(dn==2&&(va%PGSIZE)!=0)
    panic("walkfree: not aligned");
  pte_t *pte;
  int index=PX(dn,va);
  pte = &pagetable[index];
  if(dn==0){
    *pte=0;
    return PX(dn,va);
  }
  if(*pte & PTE_V) {
    pagetable = (pagetable_t)PTE2PA(*pte);
    if(uvmkunmap(pagetable,va,dn-1)==0){
   //   kfree((void *)pagetable);
   //   printf("uvmkunmap free page %p\n",pagetable);
      return PX(dn,va);
    }
    return 1;
  }
  else{
    panic("uvmkunmap:walk");
  } 
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}


// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Recursively free range of page-table pages
void 
uvmclear_range(pagetable_t pagetable,uint64 begin,uint64 end){
   // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V)){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      pagefree((pagetable_t)child);
      pagetable[i] = 0;
    }
  }
}


// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// Copy from user_pagetable to kernel pagetable
int
uvmkcopy(pagetable_t old, pagetable_t new,uint64 begin,int sz)
{
  pte_t *pte;
  uint64 pa;
  uint flags;
  uint64 addr;
  uint64 new_addr;
  //DEBUG
  //uint64 end_addr=begin+sz;
  //uint64 start_addr=PGROUNDUP(begin);

  if(sz>0){	  
  	addr=PGROUNDUP(begin);
	new_addr=PGROUNDUP(begin+sz);
  	//sz=PGROUNDUP(sz)+PGSIZE;
	for(; addr<new_addr; addr+=PGSIZE){
	  if((pte = walk(old, addr, 0)) == 0){
	    //DEBUG
      	    printf("addr=%p level2=%d level1=%d level0=%d\n",addr,PX(2,addr),PX(1,addr),PX(0,addr));
	    //printf("addr=%p i=%p sz=%p\n",addr,i,sz);
	    //vmprint_page(old,0,63);
	    //vmprint_page(old,0,62);
	    panic("uvmkcopy: pte should exist");
	  }
	  if(*pte & PTE_V){
	    pa = PTE2PA(*pte);
	    flags = PTE_FLAGS(*pte)&(~PTE_U);
  	    if(mappages(new, addr, PGSIZE, (uint64)pa, flags) != 0){
		goto err;
	    }
	  }
	}
  }
  else if(sz<0){
	//printf("start\n");
	//vmprint_page(old,0,62);
	//vmprint_page(new,0,62);
	addr=PGROUNDUP(begin);
	addr-=PGSIZE;
	new_addr=PGROUNDUP(begin+sz);
        int npages = (addr-new_addr) / PGSIZE+1;
  	for(int i=0;i<npages;i++,addr-=PGSIZE){
	  uvmkunmap(new,addr,2);
	}
	//vmprint_page(new,0,62);
  }
  return 0;

 err:
  uvmunmap(new, PGROUNDUP(begin), addr-PGROUNDUP(begin)/ PGSIZE, 0);
  return -1;
}





// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}


// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy use


// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
 /* 
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
 */
 return copyin_new(pagetable,dst,srcva, len);
 
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
 /*   
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }*/
 return copyinstr_new(pagetable,dst,srcva,max);
}

void vmprint(pagetable_t pagetable,int dn){
	//2^9=512
	if(dn<1||dn>3)
		return;
	int page_num=512;
	if(dn==1){
		page_num=256;
		printf("page table %p\n",pagetable);
	}
	for(int i=0;i<page_num;i++){
	  pte_t pte=pagetable[i];
	  if((pte&PTE_V)){
	    uint64 child=PTE2PA(pte);
    	    switch(dn){
	      case 1:
		      printf("..%d: pte %p pa %p\n",i,pte,child);
		      break;
	      case 2:
		      printf(".. ..%d: pte %p pa %p\n",i,pte,child);
		      break;
	      case 3:
		      printf(".. .. ..%d: pte %p pa %p\n",i,pte,child);
		      break;
	    }
	    vmprint((pagetable_t)child,dn+1);	    
	  }
	}
}

void vmprint_userspace(pagetable_t pagetable,int dn){
	if(dn<1||dn>3)
		return;
	int page_num=512;
	if(dn==1){
		page_num=1;
		printf("page table %p\n",pagetable);
	}
	else if(dn==2){
		page_num=PX(1,PLIC);
	}
	for(int i=0;i<page_num;i++){
	  pte_t pte=pagetable[i];
	  if((pte&PTE_V)){
	    uint64 child=PTE2PA(pte);
    	    switch(dn){
	      case 1:
		      printf("..%d: pte %p pa %p\n",i,pte,child);
		      break;
	      case 2:
		      printf(".. ..%d: pte %p pa %p\n",i,pte,child);
		      break;
	      case 3:
		      printf(".. .. ..%d: pte %p pa %p\n",i,pte,child);
		      break;
	    }
	    vmprint_userspace((pagetable_t)child,dn+1);	    
	  }
	}
}

void vmprint_page(pagetable_t pagetable,int level_2,int level_1){
	pte_t pte=0;
	pte=pagetable[level_2];
	if(!(pte&PTE_V)){
	  printf("Level 2 is not exist\n");
	  return;
	}
	pagetable=(pagetable_t)PTE2PA(pte);
	pte=pagetable[level_1];
	if(!(pte&PTE_V)){
	  printf("Level 1 is not exist\n");
	  return;
	}
	pagetable=(pagetable_t)PTE2PA(pte);
	vmprint_userspace((pagetable_t)pagetable,3);

}

void vmprint_va(uint64 va){
	printf("Level 2:%d Level 1:%d Level 0:%d\n",PX(2,va),PX(1,va),PX(0,va));
}

//Free whole kernel pagetable
void
pagefree(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      pagefree((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      pagetable[i]=0;
    }
  }
  kfree((void*)pagetable);
}
