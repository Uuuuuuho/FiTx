#include <stdio.h>

struct foo {
#ifdef CONFIG_A
    int x;
#endif
#ifdef CONFIG_B
    long y;
#endif
};

static struct foo makefoo(void) {
    struct foo f;
#ifdef CONFIG_A
    f.x = 1;
#endif
#ifdef CONFIG_B
    f.y = 2;
#endif
    return f;
}

int main(void) {
    struct foo f = makefoo();
    printf("sizeof(foo)=%zu\n", sizeof(f));
    return 0;
}
