// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

int PhysicalPageTotal = 0;
void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  uint PhisicalPageRefCount[PHYSTOP >> PGSHIFT];
} kmem;

int _physicalAddrInvalid(uint physicalAddr) {
  uint PHYSTART = V2P(end);
  return physicalAddr < PHYSTART || physicalAddr >= PHYSTOP;
}

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
  PhysicalPageTotal = 0;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  //set PhisicalPageRefCount to 0 and make it full with junk(1)
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE){
    uint physicalAddr = V2P(p);
    uint physicalPageIdx = physicalAddr >> PGSHIFT;
    kmem.PhisicalPageRefCount[physicalPageIdx] = 0;
    kfree(p);
  }
}
//PAGEBREAK: 21
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(char *v)
{
  uint physicalAddr = V2P(v);
  uint physicalPageIdx = physicalAddr >> PGSHIFT;

  int addrNoAlignToPage = (uint)physicalAddr % PGSIZE;
  int addrInvalid = _physicalAddrInvalid(physicalAddr);
  if(addrNoAlignToPage || addrInvalid)
    panic("kfree");

  if(kmem.use_lock)
    acquire(&kmem.lock);
  ///////Start kfree main work.
  struct run *r;
  r = (struct run*)v;
  if (kmem.PhisicalPageRefCount[physicalPageIdx] > 0) {
    kmem.PhisicalPageRefCount[physicalPageIdx] -= 1;
  } else if (kmem.PhisicalPageRefCount[physicalPageIdx] == 0) {
    memset(v, 1, PGSIZE);
    r->next = kmem.freelist;
    kmem.freelist = r;
    PhysicalPageTotal --;
  }
  ///////End kfree main work.
  if(kmem.use_lock)
    release(&kmem.lock);

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  if(kmem.use_lock)
    acquire(&kmem.lock);
  ///////Start kalloc main work.
  struct run *r;
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
    kmem.PhisicalPageRefCount[V2P((char*)r) >> PGSHIFT] = 1;
    PhysicalPageTotal ++;
  }
  ///////End kalloc main work.
  if(kmem.use_lock)
    release(&kmem.lock);

  return (char*)r;
}

uint getPhysicalPageRefCount(uint physicalAddr) {
  if(_physicalAddrInvalid(physicalAddr))
    panic("physicalAddr overflow in getPhysicalRefCount");

  acquire(&kmem.lock);
  ///////Start getPhysicalRefCount main work.
  uint physicalPageIdx = physicalAddr >> PGSHIFT;
  uint count = kmem.PhisicalPageRefCount[physicalPageIdx];
  ///////End getPhysicalRefCount main work.
  release(&kmem.lock);

  return count;
}

void _modifyPhysicalPageRefCount(uint physicalAddr, uint delta) {
  if(_physicalAddrInvalid(physicalAddr))
    panic("physicalAddr overflow in _modifyPhysicalPageRefCount");

  acquire(&kmem.lock);
  ///////Start modifyPhysicalPageRefCount main work.
  uint physicalPageIdx = physicalAddr >> PGSHIFT;
  kmem.PhisicalPageRefCount[physicalPageIdx] += delta;
  ///////End modifyPhysicalPageRefCount main work.
  release(&kmem.lock);
}

void increasePhysicalPageRefCountByOne(uint physicalAddr) {
  _modifyPhysicalPageRefCount(physicalAddr, 1);
}

void decreasePhysicalPageRefCountByOne(uint physicalAddr) {
  _modifyPhysicalPageRefCount(physicalAddr, -1);
}

int GetPhysicalPageTotal()
{
  return PhysicalPageTotal;
}
