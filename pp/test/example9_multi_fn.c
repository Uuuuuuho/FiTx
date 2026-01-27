#ifdef CONFIG_A
int foo(void) {
    return 1;
}
#else
int foo() {
    return 0;
}
#endif

int main() {
    return foo();
}