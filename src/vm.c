#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "traps.h"


extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}



// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.


pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i;

  if((d = setupkvm()) == 0)
    return 0;

  // Copy text, data and heap section.
  for(i = PGSIZE; i < sz; i += PGSIZE) {
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    *pte &= ~PTE_W;
    pa = PTE_ADDR(*pte);
    if(mappages(d, (void*)i, PGSIZE, pa, PTE_FLAGS(*pte)) < 0)
      goto bad;
    increasePhysicalPageRefCountByOne(pa);
  }

  // Copy stack section.
  for(i = USERTOP - myproc()->stackSize; i<USERTOP; i+=PGSIZE) {
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    *pte &= ~PTE_W;
    pa = PTE_ADDR(*pte);
    if(mappages(d, (void*)i, PGSIZE, pa, PTE_FLAGS(*pte)) < 0)
      goto bad;
    increasePhysicalPageRefCountByOne(pa);
  }

  // update cr3 register then leave.
  lcr3(V2P(pgdir));
  return d;

bad:
  freevm(d);
  lcr3(V2P(pgdir));
  return 0;
}


//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}


//以下是实现虚拟页式存储，修改的函数（allocuvm，deallocuvm，缺页中断）和辅助函数
//用到了VirtualMemory.c的函数

/*
描述：在内存表里记录一块新内存
参数：虚拟地址，当前进程
返回：无
*/
void RecordInMemory(char *TheVirtualAddress, struct proc *CurrentProcess)
{                                                                                                                                                                                    
	int i = 0;
	struct MemoryTablePage *CurrentPage = CurrentProcess->MemoryTableListHead;

	while (CurrentPage != 0)
	{
		for (i = 0; i < MEMORY_TABLE_ENTRY_NUM; i++)
		{
			if (CurrentPage->EntryList[i].VirtualAddress == SLOT_USABLE)
			{
				CurrentPage->EntryList[i].VirtualAddress = TheVirtualAddress;
				CurrentPage->EntryList[i].Next = CurrentProcess->MemoryListHead;
				if (CurrentProcess->MemoryListHead == 0)
				{
					CurrentProcess->MemoryListHead = &(CurrentPage->EntryList[i]);
					CurrentProcess->MemoryListTail = &(CurrentPage->EntryList[i]);
				}
				else
				{
					struct MemoryTableEntry* OldHead = CurrentProcess->MemoryListHead;
					CurrentProcess->MemoryListHead->Last = &(CurrentPage->EntryList[i]);
					CurrentProcess->MemoryListHead = &(CurrentPage->EntryList[i]);
					(&(CurrentPage->EntryList[i]))->Next = OldHead;
				}
				return;
			}
		}
		CurrentPage = CurrentPage -> Next;
	}
	panic("[ERROR] No free slot in memory.");
}

/*
描述：内存满了的时候，把内存优先级最低的地方扔到交换表里
参数：当前进程
返回：优先级最低的内存entry指针
*/
struct MemoryTableEntry* RecordInSwapTable(struct proc *CurrentProcess)
{
	//提取内存链表队尾
	struct MemoryTableEntry* ListTail = GetMemoryListTail(CurrentProcess);

	//提取交换表空位
	struct SwapTablePlace ThePlace = GetEmptyInSwapTable(CurrentProcess);
	struct SwapTableEntry* SwapPlace = ThePlace.Place;
	int FileOffset = ThePlace.Offset;

	//修改交换表和外存
	SwapPlace->VirtualAddress = ListTail->VirtualAddress;
	if (WriteSwapFile(CurrentProcess, (char *)PTE_ADDR(ListTail->VirtualAddress), FileOffset, PGSIZE) == 0)
	{
		return 0;
	}
	pte_t* PageTablePlace;

	//修改对应页表
	PageTablePlace = walkpgdir(CurrentProcess->pgdir, (void *)ListTail->VirtualAddress, 0);
	if (!(*PageTablePlace))
	panic("[ERROR] [fifo_write] PTE empty.");
	kfree((char *)(P2V_WO(PTE_ADDR(*PageTablePlace))));
	*PageTablePlace = PTE_W | PTE_U | PTE_PG;
	lcr3(V2P(CurrentProcess->pgdir));

	return ListTail;
}

/*
描述：交换内存表内存优先级最低的地方和交换表指定位置
参数：待进入内存的指针，当前进程
返回：无
*/
void SwapMemoryAndFile(uint TheVirtualAddress, struct proc *CurrentProcess)
{
	char SwapBuffer[SWAP_BUFFER_SIZE];
	//memory:当前在内存，要出去的;file：当前在外存，要进来的
	pte_t *PageTableMemory, *PageTableFile;

	//获取内存里的，要出去的--链表尾
	struct MemoryTableEntry* EntryMemory = GetMemoryListTail(CurrentProcess);

	//获取外存里的，要进来的
	struct SwapTablePlace ThePlace = GetAddressInSwapTable(CurrentProcess, (char *)PTE_ADDR(TheVirtualAddress));
	struct SwapTableEntry* EntryFile = ThePlace.Place;
	int FileOffset = ThePlace.Offset;



	//获取对应页表信息
	PageTableMemory = walkpgdir(CurrentProcess->pgdir, (void *)EntryMemory->VirtualAddress, 0);
	if (!*PageTableMemory)
  {
	  panic("[ERROR] A record is in memstab but not in pgdir.");
  }
	PageTableFile = walkpgdir(CurrentProcess->pgdir, (void *)TheVirtualAddress, 0);
	if (!*PageTableFile)
  {
	  panic("[ERROR] A record should be in pgdir!");
  }
	*PageTableFile = PTE_ADDR(*PageTableMemory) | PTE_U | PTE_W | PTE_P;

	//内外存交换
	int FileNum = 0;
	for (FileNum = 0; FileNum < 4; FileNum ++)
	{
		//swaptable和文件一一对应
		uint FileStartPlace = FileOffset + (SWAP_BUFFER_SIZE * FileNum);
		int CurrentWritingOffset = SWAP_BUFFER_SIZE * FileNum;
		memset(SwapBuffer, 0, SWAP_BUFFER_SIZE);
		ReadSwapFile(CurrentProcess, SwapBuffer, FileStartPlace, SWAP_BUFFER_SIZE);
		WriteSwapFile(CurrentProcess, (char *)(P2V_WO(PTE_ADDR(*PageTableMemory)) + CurrentWritingOffset), FileStartPlace, SWAP_BUFFER_SIZE);
		memmove((void *)(PTE_ADDR(TheVirtualAddress) + CurrentWritingOffset), (void *)SwapBuffer, SWAP_BUFFER_SIZE);
	}

	//更新entry,页表
	EntryFile->VirtualAddress = EntryMemory->VirtualAddress;
	SetMemoryListHead(CurrentProcess, EntryMemory, (char *)PTE_ADDR(TheVirtualAddress));
	*PageTableMemory = PTE_U | PTE_W | PTE_PG;
	lcr3(V2P(CurrentProcess->pgdir));
}

/*
描述：在内存表记录新分配的虚拟地址
参数：虚拟地址
返回：无
*/
void RecordPage(char *TheVirtualAddress)
{
	struct proc *CurrentProcess= myproc();
	RecordInMemory(TheVirtualAddress, CurrentProcess);
	CurrentProcess->MemoryEntryNum ++;
}

/*
描述：把内存优先级最低的东西扔到外存
参数：无
返回：被移除的内存页表项
*/
struct MemoryTableEntry *RecordFile(void)
{
	cprintf("Swapping out a page.\n");
	struct proc *CurrentProcess= myproc();
	return RecordInSwapTable(CurrentProcess);
}

/*
描述：把一个外存的虚拟地址和内存优先级最低地址交换
参数：无
返回：无
*/
void SwapPage(uint TheVirtualAddress)
{
	cprintf("[ INFO ] Swapping page for 0x%x.\n", TheVirtualAddress);
	struct proc *CurrentProcess = myproc();

	//? Why should we do this?
	if (kstrcmp(CurrentProcess->name, "init") == 0 || kstrcmp(CurrentProcess->name, "sh") == 0)
	{
		CurrentProcess->MemoryEntryNum ++;
		return;
	}
	SwapMemoryAndFile(TheVirtualAddress, CurrentProcess);
}

void PageFault(uint err_code)
{
  uint va = rcr2();
  struct proc* curproc = myproc();

  // If the page fault is caused by a non-present page,
  // should be due to lazy allocation or null pointer protection,
  // or stack needing growth, or due to be swapped out.
  // Otherwise, it should be due to protection violation (copy on write).
  // If the page fault is caused by kernel, it should be handled too.
  if (!(err_code & PGFLT_P))
  {
    // Used by swapping.
    pte_t* pte = &curproc->pgdir[PDX(va)];
    if(((*pte) & PTE_P) != 0)
    {
      // If the page is swapped out, swap it in.
      if(((uint*)PTE_ADDR(P2V(*pte)))[PTX(va)] & PTE_PG) 
      {
        SwapPage(PTE_ADDR(va));
        return;
      }
    }

    // If va is less than PGSIZE, this is a null pointer.
    if (va < PGSIZE)
    {
      cprintf("[ERROR] Dereferencing a null pointer (0x%x), \"%s\" will be killed.\n", va, curproc->name);
      curproc->killed = 1;
      return;
    }


    ////////////////////////Stack auto grow start////////////////////////

    uint heapBorder = curproc->sz + PGSIZE;
    uint stackBorder = USERTOP - curproc->stackSize;
    int isLackOfStackCapacity = va >= heapBorder && va < stackBorder;
    if (isLackOfStackCapacity) {
      int stackGrowResult = stackGrow(curproc->pgdir);
      if (stackGrowResult == 0) {
        cprintf("[ERROR] Stack growth failed, \"%s\" will be killed.\n", curproc->name);
        curproc->killed = 1;
      }
      return;
    }

    ////////////////////////Stack auto grow end////////////////////////



    char *mem = kalloc();
    if (mem == 0)
    {
      cprintf("Lazy allocation failed: Memory out. Killing process.\n");
      curproc->killed = 1;
      return;
    }

    // va needs to be rounded down, or two pages will be mapped in mappages().
    va = PGROUNDDOWN(va);
    memset(mem, 0, PGSIZE);

    // The first process use this page can have write permissions,
    // but once forked, copyuvm will set it permission to readonly.
    if (mappages(curproc->pgdir, (char *)va, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0)
    {
      cprintf("Lazy allocation failed: Memory out (2). Killing process.\n");
      curproc->killed = 1;
      return;
    };
  
    return;
  }

  pte_t *pte;

  if (curproc == 0)
  {
    panic("Pagefault. No process.");
  }

  if ((va >= KERNBASE) || (pte = walkpgdir(curproc->pgdir, (void *)va, 0)) == 0 || !(*pte & PTE_P) || !(*pte & PTE_U))
  {
    curproc->killed = 1;
    return;
  }








    ////////////////////////Copy on write start////////////////////////

    // If this pagefault is caused by copy on write,
    // it must has PTE_W is 0 since we clear PTE_W in copyuvm.
    if (*pte & PTE_W) {
      panic("[PageFault] This is not a copy on write pagefault. You must missed handling it above.\n");
    }
    uint pageAddr = PTE_ADDR(*pte);
    uint physicalPageRefCount = getPhysicalPageRefCount(pageAddr);
    if (physicalPageRefCount < 1) {
      panic("[PageFault:copy on write] The page isn't used by any process. You make some wrong somewhere else.\n");
    }

    if(physicalPageRefCount == 1) {
      *pte |= PTE_W;
    } else {
      char *newPage = kalloc();
      if(newPage == 0) {
        cprintf("[PageFault:copy on write] Cannot alloc new memory. Killed %s(pid %d)\n", curproc->name, curproc->pid);
        curproc->killed = 1;
        return;
      }
      memmove(newPage, (char*)P2V(pageAddr), PGSIZE);
      *pte = V2P(newPage) | PTE_P | PTE_U | PTE_W;
      decreasePhysicalPageRefCountByOne(pageAddr);
    }

    ////////////////////////Copy on write end////////////////////////





    // Flush TLB for process since page table entries changed
    lcr3(V2P(curproc->pgdir));
}


// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.

// We are using it to allocate memory from oldsz to newsz.
// Memory is not continuous now due to stack auto growth.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;
  struct proc* CurrentProcess = myproc();
  int stack_reserved = USERTOP - CurrentProcess->stackSize - PGSIZE;
  if (newsz > stack_reserved)
  {
    return 0;
  }

  if(newsz > KERNBASE)
  { 
    return 0;
  }
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE)
  {
    if(CurrentProcess -> MemoryEntryNum >= MEMORY_TABLE_TOTAL_ENTRYS)
    {
      struct MemoryTableEntry* ListTail = RecordFile();
      SetMemoryListHead(CurrentProcess, ListTail, (char*)a);
    }
    else
    {
      RecordPage((char*)a);
    }
    
    mem = kalloc();
    if(mem == 0)
    {
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    
    
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    } 
  } 
  return newsz;
}


int stackGrow(pde_t *pgdir) {
  struct proc* curproc = myproc();
  uint stackBorder = USERTOP - curproc->stackSize;
  uint heapBorder = curproc->sz;
  if (heapBorder + PGSIZE > stackBorder) {
    return 0;
  }
  char* newPageVirtualAddr = kalloc();
  if (newPageVirtualAddr == 0) {
    return 0;
  }
  uint newPagePhysicalAddr = V2P(newPageVirtualAddr);
  uint newStackBorder = stackBorder - PGSIZE;
  if (mappages(pgdir, (char*)newStackBorder, PGSIZE, newPagePhysicalAddr, PTE_W|PTE_U) < 0) {
    deallocuvm(pgdir, newStackBorder, stackBorder);
    kfree(newPageVirtualAddr);
    return 0;
  }
  if (curproc -> MemoryEntryNum >= MEMORY_TABLE_TOTAL_ENTRYS) {
    struct MemoryTableEntry* ListTail = RecordFile();
    SetMemoryListHead(curproc, ListTail, (char*)newStackBorder);
  } else {
    RecordPage((char*)newStackBorder);
  }
  memset(newPageVirtualAddr, 0, PGSIZE);
  curproc->stackSize += PGSIZE;
  return 1;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;
  struct proc* CurrentProcess = myproc();
  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
    {
      a += (NPTENTRIES - 1) * PGSIZE;
    }
    //内存中
    else if ((*pte & PTE_P) != 0)
    {
      pa = PTE_ADDR(*pte);
      if (pa == 0)
        panic("kfree");

      // If the page is in memstab, clear it.
      if (CurrentProcess->pgdir == pgdir)
      {
        struct MemoryTableEntry* CurrentEntry = GetAddressInMemoryTable(CurrentProcess, (char*)a);
        RemoveFromMemoryList(CurrentProcess, CurrentEntry);
        CurrentProcess -> MemoryEntryNum --;
      }
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
    //外存中
    else if ((*pte & PTE_PG) && CurrentProcess->pgdir == pgdir)
    {
      RemoveFromSwapTable(CurrentProcess, (char*)a);
      CurrentProcess -> SwapPageNum --;
    }
  }
  return newsz;
}
