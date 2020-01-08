/*
文件名:VirtualMemory.h
描述：虚拟页式存储数据结构和常量
*/

//全局变量定义
//内存表：2个指针，340个entry，一个entry3个指针，占用4088byte内存
//一共25个内存表，是初始化好的
//维护一个内存链表，内存链表的每个元素就是内存表里的entry，用来进行FIFO置换
//每个entry存储一个虚拟地址，代表一页（4096byte）内存
#define MEMORY_TABLE_ENTRY_NUM 340
#define MEMORY_TABLE_LENGTH 25
#define MEMORY_TABLE_TOTAL_ENTRYS (MEMORY_TABLE_ENTRY_NUM * MEMORY_TABLE_LENGTH)

//外存交换表：2个指针，1022entry，一个entry1个指针，4096byte内存
//外存表是动态维护的
//每个entry存储一个虚拟地址，代表一页（4096byte）内存
#define SWAP_TABLE_ENTRY_NUM 1022
#define SWAP_TABLE_PAGE_OFFSET (SWAP_TABLE_ENTRY_NUM * PGSIZE)

//外存表entry和外存文件线性对应
//一个外存文件最大65536bytes，相当于16个外存entry，最多6个文件，应该被初始化
//但是，一次交换只能有1024bytes被交换
#define SWAP_FILE_SIZE 65536
#define SWAP_FILE_MAX_NUM 6
#define SWAP_BUFFER_SIZE (PGSIZE / 4) 

//数据结构类型定义
struct MemoryTableEntry
{
  char *VirtualAddress;
  struct MemoryTableEntry *Next;
  struct MemoryTableEntry *Last;
};

struct SwapTableEntry
{
  char *VirtualAddress;
};

struct MemoryTablePage
{
  struct MemoryTablePage *Last;
  struct MemoryTablePage *Next;
  struct MemoryTableEntry EntryList[MEMORY_TABLE_ENTRY_NUM];
};

struct SwapTablePage
{
  struct SwapTablePage *Last;
  struct SwapTablePage *Next;
  struct SwapTableEntry EntryList[SWAP_TABLE_ENTRY_NUM];
};

struct SwapTablePlace
{
	struct SwapTableEntry* Place;
	int Offset;
};
