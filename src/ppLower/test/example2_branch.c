#include <stdio.h>

static int g = 0;

static int foo(int x) {
    return x + 1;
}

static void example2(int x) {
#ifdef CONFIG_NET
    if (x > 0) {
        g = foo(x);
    } else {
        g = foo(-x);
    }
#endif
    printf("g=%d\n", g);
}

int main(void) {
    example2(3);
    return 0;
}
