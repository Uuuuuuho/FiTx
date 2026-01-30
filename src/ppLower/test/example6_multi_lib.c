#include "example6_multi.h"

static int shared = 0;

void update_shared(int value) {
#ifdef CONFIG_SYNC
    shared += value;
#else
    shared = value;
#endif
}

int read_shared(void) {
#ifdef CONFIG_SYNC
    return shared;
#else
    return 0;
#endif
}
