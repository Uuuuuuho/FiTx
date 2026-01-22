#include <stdio.h>

#ifdef CONFIG_DEBUG
#define USE_REF(p) refcount_inc(p)
#else
#define USE_REF(p)
#endif

static int rc = 0;

static void refcount_inc(int *p) {
    (*p)++;
}

static void example5(void) {
    int x = 0;
    USE_REF(&x);
    rc = x;
}

int main(void) {
    example5();
    printf("rc=%d\n", rc);
    return 0;
}
