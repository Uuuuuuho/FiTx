#include <stdio.h>
#include <stdlib.h>

static void *kmalloc(size_t size) {
    return malloc(size);
}

static void kfree(void *ptr) {
    free(ptr);
}

static void example1(void) {
    void *p = NULL;
#ifdef CONFIG_USB
    p = kmalloc(16);
#endif
    if (p) {
        printf("p=%p\n", p);
    }
#ifdef CONFIG_USB
    kfree(p);
#endif
}

int main(void) {
    example1();
    return 0;
}
