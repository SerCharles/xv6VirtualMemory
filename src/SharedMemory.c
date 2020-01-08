/*
文件名:SharedMemory.c
描述：共享内存的函数集合
*/

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"



struct SharedMemoryEntry GlobalSharedMemoryList[SHARED_MEMORY_GLOBAL];
struct spinlock SharedMemoryLock;

/*
描述：获取全局共享内存使用情况
参数：无
返回：返回被使用的全局共享内存块数
*/
int GetGlobalSharedMemoryInfo()
{
    int i;
    int UsedMemoryNum = 0;
    for (i = 0; i < SHARED_MEMORY_GLOBAL; i++)
    {
        if (GlobalSharedMemoryList[i].Signature > 0)
        {
            UsedMemoryNum ++;
        }
    }
    return UsedMemoryNum;
}

/*
描述：获取进程共享内存使用情况
参数：无
返回：返回被使用的进程共享内存块数
*/
int GetProcessSharedMemoryInfo(struct proc* CurrentProcess)
{
    int i;
    int UsedMemoryNum = 0;
    for (i = 0; i < SHARED_MEMORY_PER_PROC; i++)
    {
        if (CurrentProcess->SelfSharedMemory[i] > 0)
        {
            UsedMemoryNum ++;
        }
    }
    return UsedMemoryNum;
}


/*
描述：在本进程找一个共享内存
参数：当前进程，信号
返回：成功：位置，失败：-1
*/
int FindSelfSharedMemory(struct proc* CurrentProcess, int TheSignature)
{
    int i;
    for (i = 0; i < SHARED_MEMORY_PER_PROC; i++)
    {
        if (CurrentProcess->SelfSharedMemory[i] == TheSignature)
        {
            return i;
        }
    }
    return -1;
}

/*
描述：在全局找一个共享内存
参数：信号
返回：成功：位置，失败：-1
*/
int FindGlobalSharedMemory(int TheSignature)
{
    int i;
    for (i = 0; i < SHARED_MEMORY_GLOBAL; i++)
    {
        if (GlobalSharedMemoryList[i].Signature == TheSignature)
        {
            return i;
        }
    }
    return -1;
}

/*
描述：在初始化阶段，初始化全局共享内存为空
参数：无
返回：无
*/
void InitGlobalSharedMemory(void)
{
  int i;
  for (i = 0; i < SHARED_MEMORY_GLOBAL; i++)
  {
    GlobalSharedMemoryList[i].VirtualAddress = 0;
    GlobalSharedMemoryList[i].Signature = 0;
    GlobalSharedMemoryList[i].UserNumber = 0;
  }
}

/*
描述：分配某个进程共享内存
参数：信号
返回：成功0失败-1
*/
int AllocSharedMemory(int TheSignature)
{
    acquire(&SharedMemoryLock);
    struct proc *CurrentProcess = myproc();
    int i = 0, SelfEmptyPlace = -1, GlobalEmptyPlace = -1;


    //先找自己有没有这一块内存,如果有，报错返回，否则找一块空位置
    for (i = 0; i < SHARED_MEMORY_PER_PROC; i++)
    {
        if (CurrentProcess->SelfSharedMemory[i] == TheSignature)
        {
            release(&SharedMemoryLock);
            return -1;
        }
        if (CurrentProcess->SelfSharedMemory[i] == 0)
        {
            SelfEmptyPlace = i;
        }
    }

    //自己没有空位了
    if (SelfEmptyPlace == -1)
    {
        release(&SharedMemoryLock);
        return -1;
    }


    //找有没有分配好的，有自己直接记录,否则找一块空位记录
    for (i = 0; i < SHARED_MEMORY_GLOBAL; i++)
    {
        if (GlobalSharedMemoryList[i].Signature == TheSignature)
        {
            CurrentProcess->SelfSharedMemory[SelfEmptyPlace] = TheSignature;
            GlobalSharedMemoryList[i].UserNumber++;
            release(&SharedMemoryLock);
            return 0;
        }
        if (GlobalSharedMemoryList[i].Signature== 0)
        {
            GlobalEmptyPlace = i;
        }
    }

    //在全局分配一块新的共享内存，并且给这个进程
    if(GlobalEmptyPlace != -1)
    {
        GlobalSharedMemoryList[GlobalEmptyPlace].VirtualAddress = kalloc();
        GlobalSharedMemoryList[GlobalEmptyPlace].Signature = TheSignature;
        GlobalSharedMemoryList[GlobalEmptyPlace].UserNumber = 1;
        CurrentProcess->SelfSharedMemory[SelfEmptyPlace] = TheSignature;
        release(&SharedMemoryLock);
        return 0;
    }
    release(&SharedMemoryLock);
    return -1;
}


/*
描述：释放某个进程的共享内存
参数：信号
返回：成功0失败-1
*/
int DeallocSharedMemory(int TheSignature)
{
    acquire(&SharedMemoryLock);
    struct proc* CurrentProcess = myproc();
    int SelfPlace = -1, GlobalPlace = -1;

    //先找自己有没有
    SelfPlace = FindSelfSharedMemory(CurrentProcess, TheSignature);

    //自己没有
    if (SelfPlace == -1)
    {
        release(&SharedMemoryLock);
        return -1;
    }

    //在列表里找,有就释放
    GlobalPlace = FindGlobalSharedMemory(TheSignature);

    //列表里有
    if(GlobalPlace != -1)
    {
        GlobalSharedMemoryList[GlobalPlace].UserNumber --;
        CurrentProcess->SelfSharedMemory[SelfPlace] = 0;
        if(GlobalSharedMemoryList[GlobalPlace].UserNumber <= 0)
        {
            kfree(GlobalSharedMemoryList[GlobalPlace].VirtualAddress);
            GlobalSharedMemoryList[GlobalPlace].VirtualAddress = (void *)-1;
            GlobalSharedMemoryList[GlobalPlace].Signature = 0;
        }
        release(&SharedMemoryLock);
        return 0;
    }

    //列表里没有
    release(&SharedMemoryLock);
    return -1;
}


/*
描述：读取某个进程的共享内存
参数：信号，缓冲区
返回：成功0失败-1
*/
int ReadSharedMemory(int TheSignature, char *TheBuffer)
{
    struct proc* CurrentProcess = myproc();
    int SelfPlace = -1, GlobalPlace = -1;
    
    //先找自己有没有
    SelfPlace = FindSelfSharedMemory(CurrentProcess, TheSignature);
    if(SelfPlace == -1)
    {
        return -1;
    }

    //找全局有没有
    GlobalPlace = FindGlobalSharedMemory(TheSignature);
    if (GlobalPlace != -1)
    {
        memmove(TheBuffer, GlobalSharedMemoryList[GlobalPlace].VirtualAddress, PGSIZE);
        return 0;
    }
    return -1;
}

/*
描述：写入某个进程的共享内存
参数：信号，缓冲区
返回：成功0失败-1
*/
int WriteSharedMemory(int TheSignature, char *TheBuffer)
{
    struct proc* CurrentProcess = myproc();
    int SelfPlace, GlobalPlace;
    SelfPlace = FindSelfSharedMemory(CurrentProcess, TheSignature);
    if (SelfPlace == -1)
    {
        return -1;
    }
    GlobalPlace = FindGlobalSharedMemory(TheSignature);
    if(GlobalPlace != -1)
    {
        int Length = strlen(TheBuffer);
        strncpy(GlobalSharedMemoryList[GlobalPlace].VirtualAddress, TheBuffer, Length + 1);
        return 0;
    }
    return -1;
}
