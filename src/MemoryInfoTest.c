#include "types.h"
#include "stat.h"
#include "user.h"

/*
描述：char转unit函数
参数: char数组头指针
返回：uint数
*/
unsigned int CharToInt(char* ResultList)
{
    unsigned int Number = 0;
    Number += (ResultList[0] << 24);
    Number += (ResultList[1] << 16);
    Number += (ResultList[2] << 8);
    Number += (ResultList[3]);
    return Number;
}


void MemoryInfoObtain(void)
{
    char ResultList[1200];
    int i = 0;
    for(i = 0; i < 1200; i ++)
    {
        ResultList[i] = 0;
    }
    GetMemoryInfo(ResultList);
    unsigned int ProcessNumber = CharToInt(&ResultList[0]);
    unsigned int PhysicalMemoryUsed = CharToInt(&ResultList[4]) * 4;
    unsigned int SharedMemoryUsed = CharToInt(&ResultList[8]) * 4;

    printf(1, "Total Processes: %d; Total Physical Memory Used: %dkb; Total Shared Memory User: %dkb\n", ProcessNumber, PhysicalMemoryUsed, SharedMemoryUsed);
    for(int i = 1; i <= ProcessNumber; i ++)
    {
        int Base = i * 16;
        unsigned int ProcessID = CharToInt(&ResultList[Base]);
        unsigned int MemoryPageUsed = CharToInt(&ResultList[Base + 4]) * 4;
        unsigned int SwapPageUsed = CharToInt(&ResultList[Base + 8]) * 4;
        unsigned int SharedPageUsed = CharToInt(&ResultList[Base + 12]) * 4;
        printf(1, "Process %d: Physical Memory Used: %dkb; Swap Memory Used: %dkb, Shared Memory Used: %dkb\n", ProcessID, MemoryPageUsed, SwapPageUsed, SharedPageUsed);
    }    
}

int main()
{
    MemoryInfoObtain();
    int* a = (int*)malloc(12000 * sizeof(int));
    AllocSharedMemory(114514);
    MemoryInfoObtain();
    DeallocSharedMemory(114514);
    free(a);
    return 0;
}