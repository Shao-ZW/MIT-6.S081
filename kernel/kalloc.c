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
  struct spinlock lock[NCPU];
  struct run *freelist[NCPU];
} kmem;

void
kinit()
{
  for(int i = 0; i < NCPU; ++i)
    initlock(&kmem.lock[i], "kmem");
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
  int hart = cpuid();
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock[hart]);
  r->next = kmem.freelist[hart];
  kmem.freelist[hart] = r;
  release(&kmem.lock[hart]);
  pop_off();
}

//try to steal the other freelist when empty
//must hold the lock and disabled interrupt
void 
steal(int hart)
{
  for(int i = 0; i < NCPU; ++i){
    if(i == hart) 
      continue;
    acquire(&kmem.lock[i]);
    if(kmem.freelist[i]){
      struct run *r = kmem.freelist[i];
      kmem.freelist[i] = r->next;
      r->next = kmem.freelist[hart];
      kmem.freelist[hart] = r;
    }
    release(&kmem.lock[i]);
  }
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  push_off();
  int hart = cpuid();
  struct run *r;

  acquire(&kmem.lock[hart]);
  if(!kmem.freelist[hart])
    steal(hart);
  r = kmem.freelist[hart];
  if(r)
    kmem.freelist[hart] = r->next;
  release(&kmem.lock[hart]);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  pop_off();

  return (void*)r;
}