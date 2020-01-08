#include "types.h"
#include "stat.h"
#include "user.h"

int main()
{
    int *a = 0;
    printf(1, "================================\n");
    printf(1, "Zero pointer protection test started.\n");
    printf(1, "This process should be killed which means the test is successful.\n");
    printf(1, "The value of zero pointer is %d.\n", *a);
    printf(1, "The process should be killed but is alive.\n");
    printf(1, "Zero pointer protection test failed.\n");
    return 0;
}
