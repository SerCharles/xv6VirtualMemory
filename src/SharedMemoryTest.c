#include "types.h"
#include "stat.h"
#include "user.h"

int Signature = 114514;
int main()
{
    printf(1, "================================\n");
    printf(1, "Shared memory test started.\n");

    if (AllocSharedMemory(Signature) == 0)
    {
        printf(1, "[P] Share memory created.\n");
    }
    else
    {
        printf(1, "[P] Share memory creating failed.\n");
        exit();
    }

    char *WriteBuffer = "This is parent's memory info.";
    printf(1, "[P] Writing message to child...\n");
    if (WriteSharedMemory(Signature, WriteBuffer) != 0)
    {
        printf(1, "[P] Father write Error!\n");
        exit();
    }

    if (fork() == 0) // This is child.
    {
        //printf(1,"kebab\n");
        if (AllocSharedMemory(Signature) == 0)
        {
            printf(1, "[C] Share memory created.\n");
        }
        else
        {
            printf(1, "[C] Share memory creating failed.\n");
            exit();
        }
        char *ReadBuffer = malloc(4096);
        if (ReadSharedMemory(Signature, ReadBuffer) != 0)
        {
            printf(1, "[C] Child read Error!\n");
            free(ReadBuffer);
            exit();
        }
        printf(1, "[C] Recv: %s\n", ReadBuffer);
        char *NewWriteBuffer = "This is child's memory info.";
        printf(1, "[C] Writing message to parent...\n");
        if (WriteSharedMemory(Signature, NewWriteBuffer) != 0)
        {
            printf(1, "[C] Child write Error!\n");
            free(ReadBuffer);
            exit();
        }

        if (DeallocSharedMemory(Signature) == 0)
        printf(1, "[C] Share memory removed.\n");
        else
        {
            printf(1, "[C] Share memory removing failed.\n");
            exit();
        }
        free(ReadBuffer);
        printf(1, "Child start sleeping\n");
        sleep(100);
        exit();
    }
    else // This is parent.
    {
        printf(1, "Parent start sleeping\n");
        sleep(100);
        char *ReadBuffer = malloc(4096);
        if (ReadSharedMemory(Signature, ReadBuffer) != 0)
        {
            printf(1, "[P] Father read fail !\n");
            free(ReadBuffer);
            exit();
        }
        printf(1, "[P] Recv: %s\n", ReadBuffer);
        free(ReadBuffer);
        wait();
    }

    if (DeallocSharedMemory(Signature) == 0)
    {
        printf(1, "[P] Share memory removed.\n");
    }
    else
    {
        printf(1, "[P] Share memory removing failed.\n");
        exit();
    }

    {
        char *ReadBuffer = malloc(4096);
        if (ReadSharedMemory(Signature, ReadBuffer) != 0)
        {
            printf(1, "[P] Father read fail !\n");
            free(ReadBuffer);
        }
        else
        {
            printf(1, "[P] Recv: %s\n", ReadBuffer);
            free(ReadBuffer);
        }
    }

    printf(1, "Shared memory test finished.\n");
    printf(1, "================================\n");
    return 0;
}