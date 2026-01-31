// test_inactive.c

int used_function() {
    return 0;
}

void caller() {
#ifdef CONFIG_A
    // inactive code: should be annotated by plugin
    void unreachable() {
        int x = 1;
    }
#endif
    used_function();
}

int main() {
    caller();
    return 0;
}
