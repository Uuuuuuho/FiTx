#include <stdio.h>

#include "example6_multi.h"

int main(void) {
#ifdef CONFIG_SYNC
    update_shared(1);
    update_shared(2);
#else
    update_shared(3);
#endif
    printf("shared=%d\n", read_shared());
    return 0;
}
