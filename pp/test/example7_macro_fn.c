#ifdef CONFIG_A
int foo(void) {
    return 1;
}
#else
#define foo() (0)
#endif

int main() {
    return foo();
}