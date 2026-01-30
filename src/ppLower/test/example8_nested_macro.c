#ifdef CONFIG_A
#define CONFIG_B 1
#endif

int foo() {
    return CONFIG_B;
}

int main() {
    foo();
    return 0;
}