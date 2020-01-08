#include "types.h"
#include "stat.h"
#include "user.h"

int sharedData = 2222;

int main()
{
    printf(1, "================================\n");
    printf(1, "[GlobalNotification] Test copy on write begin.\n");
    printf(1, "[ParentSay] My global data is [%d]\n", sharedData);
    printf(1, "[ParentSay] I will fork a child process then sleep 200ms.\n");
    printf(1, "[GlobalNotification] Parent fork itself. A baby was born.\n");
    if (fork() == 0) {
        printf(1, "[ChildSay] I was born! My global data is [%d]. I will change my data now.\n", sharedData);
        sharedData = 8888;
        printf(1, "[GlobalNotification] Child modified its global data to 8888.\n");
        printf(1, "[ChildSay] Now my global data is [%d]. Is my parent's data affected by my modification?\n", sharedData);
        exit();
    }
    sleep(200);
    wait();
    printf(1, "[ParentSay] My global data is still [%d], you cann't affect my data.\n", sharedData);
    printf(1, "[GlobalNotification] Test copy on write was done.\n");
    printf(1, "================================\n");


    sharedData = 2222;
    printf(1, "================================\n");
    printf(1, "[GlobalNotification] Test recursive copy on write begin.\n");
    printf(1, "[GrandParentSay] My global data is [%d]\n", sharedData);
    printf(1, "[GrandParentSay] I will fork a child process then sleep 200ms.\n");
    printf(1, "[GlobalNotification] GrandParent fork itself. Parent was born.\n");
    if (fork() == 0) {
        printf(1, "[ParentSay] I was born! I will fork a child process then sleep 100ms.\n", sharedData);
        if (fork() == 0) {
          printf(1, "[ChildSay] I was born! My global data is [%d]. I will change my data now.\n", sharedData);
          sharedData = 9999999;
          printf(1, "[GlobalNotification] Child modified its global data to 8888.\n");
          printf(1, "[ChildSay] Now my global data is [%d]. Is my parent's data affected by my modification?\n", sharedData);
          exit();
        }
        sleep(100);
        printf(1, "[ParentSay] My global data is still [%d], you cann't affect my data. I will change my data now.\n", sharedData);
        sharedData = 8888;
        printf(1, "[GlobalNotification] Parent modified its global data to 8888.\n");
        printf(1, "[ParentSay] Now my global data is [%d]. Is grandparent's data affected by my modification?\n", sharedData);
        exit();
    }
    sleep(200);
    wait();
    printf(1, "[GrandParentSay] My global data is still [%d], you cann't affect my data.\n", sharedData);
    printf(1, "[GlobalNotification] Test recursive copy on write was done.\n");
    printf(1, "================================\n");

    return 0;
}