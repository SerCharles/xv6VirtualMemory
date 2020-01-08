#include "types.h"
#include "stat.h"
#include "user.h"

int sig = 23333333;
int main()
{
    printf(1, "================================\n");
    printf(1, "Memory sharing test started.\n");

    if (mkshm(sig) == 0)
        printf(1, "[P] Share memory created.\n");
    else
    {
        printf(1, "[P] Share memory creating failed.\n");
        exit();
    }

    char *content = "Hello child proc, I'm your father!";
    printf(1, "[P] Writing message to child...\n");
    if (wtshm(sig, content) != 0)
    {
        printf(1, "[P] Father write Error!\n");
        exit();
    }

    if (fork() == 0) // This is child.
    {
        //printf(1,"kebab\n");
        if (mkshm(sig) == 0)
        printf(1, "[C] Share memory created.\n");
        else
        {
            printf(1, "[C] Share memory creating failed.\n");
            exit();
        }
        char *read = malloc(4096);
        if (rdshm(sig, read) != 0)
        {
            printf(1, "[C] Child read Error!\n");
            free(read);
            exit();
        }
        printf(1, "[C] Recv: %s\n", read);
        char *write = "Hello parent proc, I'm your child!";
        printf(1, "[C] Writing message to parent...\n");
        if (wtshm(sig, write) != 0)
        {
            printf(1, "[C] Child write Error!\n");
            free(read);
            exit();
        }

        if (rmshm(sig) == 0)
        printf(1, "[C] Share memory removed.\n");
        else
        {
            printf(1, "[C] Share memory removing failed.\n");
            exit();
        }
        free(read);
        printf(1, "Child start sleeping\n");
        sleep(100);
        exit();
    }
    else // This is parent.
    {
        printf(1, "Parent start sleeping\n");
        sleep(100);
        char *read = malloc(4096);
        if (rdshm(sig, read) != 0)
        {
            printf(1, "[P] Father read fail !\n");
            free(read);
            exit();
        }
        printf(1, "[P] Recv: %s\n", read);
        free(read);
        wait();
    }

    if (rmshm(sig) == 0)
        printf(1, "[P] Share memory removed.\n");
    else
    {
        printf(1, "[P] Share memory removing failed.\n");
        exit();
    }

    {
        char *read = malloc(4096);
        if (rdshm(sig, read) != 0)
        {
            printf(1, "[P] Father read fail !\n");
            free(read);
            //exit();
        }
        else
        {
        printf(1, "[P] Recv: %s\n", read);
        free(read);
        }
    }

    printf(1, "Memory sharing test finished.\n");
    printf(1, "================================\n");
    return 0;
}