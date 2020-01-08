/*
文件名:VirtualMemory.c
描述：维护虚拟页式存储数据结构的函数集合，包括进程生命周期中的初始化，复制，删除，也包括内存管理中的修改和维护
*/


#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"


//以下是虚拟内存数据结构的动态修改，更新函数，主要用于虚拟内存的具体管理---内存分配，释放，处理缺页中断
/*
描述：提取并移除内存链表尾，并且更新链表
参数：当前进程
返回：链表尾
*/
struct MemoryTableEntry* GetMemoryListTail(struct proc *CurrentProcess)
{
	struct MemoryTableEntry *CurrentEntry, *ListTail;
	CurrentEntry = CurrentProcess->MemoryListHead;
	if (CurrentEntry == 0 || CurrentEntry->Next == 0)
    {
	    panic("[ERROR] It is impossible for the memory list used to be full!");
    }
	ListTail = CurrentProcess->MemoryListTail;
	if (ListTail == 0 || ListTail->Last == 0)
    {
	    panic("[ERROR] The Last of the memory list should not be NULL!");
    }
	CurrentProcess->MemoryListTail = ListTail->Last;
	ListTail->Last->Next = 0;
	ListTail->Last = 0;
	return ListTail;
}

/*
描述：将一个entry地址设置为指定地址，并且设置为链表头
参数：当前进程，新头entry，新头地址
返回：无
*/
void SetMemoryListHead(struct proc *CurrentProcess, struct MemoryTableEntry* TheEntry, char* TheVirtualAddress)
{
	TheEntry->Next = CurrentProcess->MemoryListHead;
	CurrentProcess->MemoryListHead->Last = TheEntry;
	CurrentProcess->MemoryListHead = TheEntry;
	TheEntry->VirtualAddress = TheVirtualAddress;
}

/*
描述：将一个entry地址设置为可用，并且移除出链表
参数：当前进程，entry
返回：无
*/
void RemoveFromMemoryList(struct proc *CurrentProcess, struct MemoryTableEntry* TheEntry)
{
	TheEntry->VirtualAddress = SLOT_USABLE;
    if (CurrentProcess->MemoryListHead == TheEntry)
    {
        if (TheEntry->Next != 0)
		{
        	TheEntry->Next->Last = 0;
		}
        CurrentProcess->MemoryListHead = TheEntry->Next;
    }
    else
    {
        struct MemoryTableEntry *CurrentPlace = CurrentProcess->MemoryListHead;
        while (CurrentPlace->Next != TheEntry)
		{
            CurrentPlace = CurrentPlace->Next;
		}
		if(TheEntry->Next != 0)
		{
			TheEntry->Next->Last = CurrentPlace;
			CurrentPlace->Next = TheEntry->Next;
		}
		else
		{
			CurrentPlace->Next = 0;
		}

    }
    TheEntry->Next = 0;
    TheEntry->Last = 0;
    CurrentProcess->MemoryEntryNum --;
}


/*
描述：在内存表里找对应位置
参数：当前进程，虚拟地址
返回：空位(包括位置，偏置)
*/
struct MemoryTableEntry* GetAddressInMemoryTable(struct proc *CurrentProcess, char* TheVirtualAddress)
{
	struct MemoryTablePage *CurrentPage;
	struct MemoryTableEntry* ThePlace;
	int EntryNum = 0, PageNum = 0;

	//在原来swaptable找
	CurrentPage = CurrentProcess->MemoryTableListHead;
	while (CurrentPage != 0)
	{
		for (EntryNum = 0; EntryNum < MEMORY_TABLE_ENTRY_NUM; EntryNum ++)
		{
			if (CurrentPage->EntryList[EntryNum].VirtualAddress == TheVirtualAddress)
			{
				ThePlace = &(CurrentPage->EntryList[EntryNum]);
				return ThePlace;
			}
		}
		CurrentPage = CurrentPage->Next;
		PageNum ++;
	}
	panic("[ERROR] Should find a record in memory table!");
}


/*
描述：在交换表里找空位，没有就新开一个空的位置
参数：当前进程
返回：空位(包括位置，偏置)
*/
struct SwapTablePlace GetEmptyInSwapTable(struct proc *CurrentProcess)
{
	struct SwapTablePage *CurrentPage;
	struct SwapTablePlace ThePlace;
	int EntryNum = 0, PageNum = 0;

	//在原来swaptable找
	CurrentPage = CurrentProcess->SwapTableListHead;
	while (CurrentPage != 0)
	{
		for (EntryNum = 0; EntryNum < SWAP_TABLE_ENTRY_NUM; EntryNum ++)
		{
			if (CurrentPage->EntryList[EntryNum].VirtualAddress == SLOT_USABLE)
			{
				ThePlace.Place = &(CurrentPage->EntryList[EntryNum]);
				ThePlace.Offset = PageNum * SWAP_TABLE_PAGE_OFFSET + EntryNum * PGSIZE;
				return ThePlace;
			}
		}
		CurrentPage = CurrentPage->Next;
		PageNum ++;
	}

	//找不到：新增一页继续
	GrowSwapTable(CurrentProcess);
	CurrentPage = CurrentProcess->SwapTableListTail;
	for (EntryNum = 0; EntryNum < SWAP_TABLE_ENTRY_NUM; EntryNum ++)
	{
		if (CurrentPage->EntryList[EntryNum].VirtualAddress == SLOT_USABLE)
		{
			ThePlace.Place = &(CurrentPage->EntryList[EntryNum]);
			ThePlace.Offset = PageNum * SWAP_TABLE_PAGE_OFFSET + EntryNum * PGSIZE;
			return ThePlace;
		}
	}
	panic("[ERROR] Should find an empty place in the swap table!");
}

/*
描述：在交换表里找对应位置
参数：当前进程，位置
返回：空位(包括位置，偏置)
*/
struct SwapTablePlace GetAddressInSwapTable(struct proc *CurrentProcess, char* TheVirtualAddress)
{
	struct SwapTablePage *CurrentPage;
	struct SwapTablePlace ThePlace;
	int EntryNum = 0, PageNum = 0;

	//在原来swaptable找
	CurrentPage = CurrentProcess->SwapTableListHead;
	while (CurrentPage != 0)
	{
		for (EntryNum = 0; EntryNum < SWAP_TABLE_ENTRY_NUM; EntryNum ++)
		{
			if (CurrentPage->EntryList[EntryNum].VirtualAddress == TheVirtualAddress)
			{
				ThePlace.Place = &(CurrentPage->EntryList[EntryNum]);
				ThePlace.Offset = PageNum * SWAP_TABLE_PAGE_OFFSET + EntryNum * PGSIZE;
				return ThePlace;
			}
		}
		CurrentPage = CurrentPage->Next;
		PageNum ++;
	}
	panic("[ERROR] Should find the place in the swap table!");
}

/*
描述：将一个虚拟地址对应的交换表entry地址设置为可用
参数：当前进程，地址
返回：无
*/
void RemoveFromSwapTable(struct proc *CurrentProcess, char* TheVirtualAddress)
{
	struct SwapTablePlace ThePlace = GetAddressInSwapTable(CurrentProcess, TheVirtualAddress);
	struct SwapTableEntry* TheEntry = ThePlace.Place;
	TheEntry->VirtualAddress = SLOT_USABLE;
}


//以下是虚拟内存数据结构的初始化，复制，清理等函数
/*
描述：清理一页内存表
参数：一页表的地址，是否清理链接
返回：无
*/
void ClearMemoryPage(struct MemoryTablePage *ThePage, uint WhetherClearLink)
{
    int i;
    for (i = 0; i < MEMORY_TABLE_ENTRY_NUM; i++)
    {
        ThePage->EntryList[i].Last = 0;
        ThePage->EntryList[i].Next = 0;
        ThePage->EntryList[i].VirtualAddress = SLOT_USABLE;
    }
    if (WhetherClearLink)
    {
        ThePage->Next = 0;
        ThePage->Last = 0;
    }
}

/*
描述：生成一页内存表
参数：无
返回：一页表
*/
struct MemoryTablePage *AllocMemoryPage(void)
{
    struct MemoryTablePage *NewPage;
    if ((NewPage = (struct MemoryTablePage *)kalloc()) == 0)
    {
        return 0;
    }
    ClearMemoryPage(NewPage, 1);
    return NewPage;
}

/*
描述：清理一个进程的内存表
参数：进程
返回：无
*/
void ClearMemoryTable(struct proc *CurrentProcess)
{
    struct MemoryTablePage *CurrentPage = CurrentProcess->MemoryTableListHead;
    while (CurrentPage != 0)
    {
        ClearMemoryPage(CurrentPage, 0);
        CurrentPage = CurrentPage->Next;
    }
    CurrentProcess->MemoryEntryNum = 0;
    CurrentProcess->MemoryListHead = 0;
    CurrentProcess->MemoryListTail = 0;
}

/*
描述：初始化一个进程的内存表
参数：进程
返回：无
*/
void AllocMemoryTable(struct proc *CurrentProcess)
{
    int i;
    struct MemoryTablePage *CurrentPage, *PreviousPage;
    for (i = 0; i < MEMORY_TABLE_LENGTH; i++)
    {
        if ((CurrentPage = AllocMemoryPage()) == 0)
        {
            panic("Alloc Memory Table Failure!\n");
        }
        if(i == 0)
        {
            CurrentProcess->MemoryTableListHead = CurrentPage;
            PreviousPage = CurrentPage;
        }
        else
        {
            PreviousPage->Next = CurrentPage;
            CurrentPage->Last = PreviousPage;
            PreviousPage = CurrentPage;
        }
    }
    CurrentProcess->MemoryTableListTail = CurrentPage;
}

/*
描述：清理一页交换表
参数：一页表的地址，是否清理链接
返回：无
*/
void ClearSwapPage(struct SwapTablePage *CurrentPage, uint WhetherClearLink)
{
    int i;
    for (i = 0; i < SWAP_TABLE_ENTRY_NUM; i++)
    {
        CurrentPage->EntryList[i].VirtualAddress = SLOT_USABLE;
    }
    if (WhetherClearLink)
    {
        CurrentPage->Next = 0;
        CurrentPage->Last = 0;
    }
}

/*
描述：生成一页交换表
参数：无
返回：一页表
*/
struct SwapTablePage* AllocSwapPage(void)
{
    struct SwapTablePage *NewPage;
    if ((NewPage = (struct SwapTablePage *)kalloc()) == 0)
    {
        return 0;
    }
    ClearSwapPage(NewPage, 1);
    return NewPage;
}

/*
描述：清理一个进程的交换表
参数：进程
返回：无
*/
void ClearSwapTable(struct proc *CurrentProcess)
{
  	struct SwapTablePage *CurrentPage;
  	CurrentPage = CurrentProcess->SwapTableListHead;
	while (CurrentPage != 0)
  	{
    	ClearSwapPage(CurrentPage, 0);
    	CurrentPage = CurrentPage->Next;
  	}
}

/*
描述：分配一个进程的内存表
参数：进程
返回：成功0失败-1
*/
int GrowSwapTable(struct proc *CurrentProcess)
{
  	struct SwapTablePage **head, **tail;
  	head = &(CurrentProcess->SwapTableListHead);
  	tail = &(CurrentProcess->SwapTableListTail);
  
  	if (*head == 0)
  	{
    	if ((*head = AllocSwapPage()) == 0)
    	{
        	return -1;
    	}
    	*tail = *head;
  	}
  	else
  	{
    	struct SwapTablePage *temp = *tail;
    	if ((*tail = AllocSwapPage()) == 0)
    	{
        	return -1;
    	}
    	temp->Next = *tail;
    	(*tail)->Last = temp;
  	}

  	return 0;
}

/*
描述：复制交换表和交换文件等
参数：目的地，源
返回：成功0失败-1
*/
int CopyVirtualMemoryData(struct proc *Destination, struct proc *Source)
{
	//复制交换表
  	ClearMemoryTable(Destination);
  	Destination->MemoryEntryNum = Source->MemoryEntryNum;
  	Destination->MemoryListHead = 0;
  	Destination->MemoryListTail = 0;

  	struct MemoryTablePage *CurrentDestinationPage = Destination->MemoryTableListHead;
  	int DestinationEntryNum = 0;
  	struct MemoryTableEntry *CurrentSourceEntry = Source->MemoryListHead;
  	struct MemoryTableEntry *PreviousDestinationEntry = 0;

  	if (CurrentSourceEntry != 0)
  	{
    	Destination->MemoryListHead = &(CurrentDestinationPage->EntryList[DestinationEntryNum]);
  	}
  	while (CurrentSourceEntry != 0)
  	{
    	if (PreviousDestinationEntry != 0)
    	{
      		PreviousDestinationEntry->Next = &(CurrentDestinationPage->EntryList[DestinationEntryNum]);
    	}
    	CurrentDestinationPage->EntryList[DestinationEntryNum].Last = PreviousDestinationEntry;
    	PreviousDestinationEntry = &(CurrentDestinationPage->EntryList[DestinationEntryNum]);
    	CurrentDestinationPage->EntryList[DestinationEntryNum].VirtualAddress = CurrentSourceEntry->VirtualAddress;

    	CurrentSourceEntry = CurrentSourceEntry->Next;
    	DestinationEntryNum++;

    	if (DestinationEntryNum == MEMORY_TABLE_ENTRY_NUM)
    	{
      		CurrentDestinationPage = CurrentDestinationPage->Next;
      		DestinationEntryNum = 0;
    	}
  	}
  	Destination->MemoryListTail = PreviousDestinationEntry;

	//复制交换表
  	int i;
  	struct SwapTablePage *SourceSwapPage, *DestinationSwapPage;
  	while (Source->SwapPageNum > Destination->SwapPageNum)
  	{
    	if (GrowSwapTable(Destination) == 0)
    	{
      	return -1;
    	}
    	Destination->SwapPageNum ++;
  	}

  	SourceSwapPage = Source->SwapTableListHead;
  	DestinationSwapPage = Destination->SwapTableListHead;
  	while (SourceSwapPage != 0)
  	{
    	for (i = 0; i < SWAP_TABLE_ENTRY_NUM; i++)
    	{
      	DestinationSwapPage->EntryList[i] = SourceSwapPage->EntryList[i];
    	}
    	DestinationSwapPage = DestinationSwapPage->Next;
    	SourceSwapPage = SourceSwapPage->Next;
  	}
  	return 0;
}