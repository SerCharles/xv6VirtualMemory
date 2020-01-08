#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_PER_CALL 1024
#define CALLS 7359

void OneCall(int n)
{
    if (n == 0)
    {
        return;
    }
    else if (n >= 1000)
    {   
        if(n % 1000 == 0)
        {
            printf(1, "Calls Remain: %d\n", n);
        }
    }
    else if (n >= 50)
    {
        if(n % 50 == 0)
        {
            printf(1, "Calls Remain: %d\n", n);
        }
    }
    else
    {
        printf(1, "Calls Remain: %d\n", n);
    }
    int * MemoryAlloced = (int*)malloc(sizeof(int) * NUM_PER_CALL);
    int i;
    for (i = 0; i < NUM_PER_CALL; i++)
    {
        (void)(MemoryAlloced[i] = i);
    }
    OneCall(n - 1);
    free(MemoryAlloced);
}

int main()
{
    printf(1, "================================\n");
    printf(1, "Virtual Memory test started.\n");

    OneCall(CALLS);

    printf(1, "Virtual Memory test finished.\n");
    printf(1, "================================\n");
    return 0;
}
