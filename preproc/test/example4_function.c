#include <stdio.h>

#ifdef CONFIG_X
int foo(void) {
    return 42;
}
#endif

static int callfoo(void) {
#ifdef CONFIG_X
    return foo();
#else
    return -1;
#endif
}

int main(void) {
    printf("callfoo=%d\n", callfoo());
    return 0;
}
