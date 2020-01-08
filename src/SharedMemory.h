/*
文件名:SharedMemory.h
描述：共享内存的常量和结构体定义
*/
#define SHARED_MEMORY_PER_PROC 8
#define SHARED_MEMORY_GLOBAL 256

struct SharedMemoryEntry
{
  void *VirtualAddress;
  //签名，唯一标识
  int Signature;
  int UserNumber;
};
