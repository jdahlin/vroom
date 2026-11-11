/* Labels do not introduce scopes: declarations following them remain alive
 * until the enclosing block ends, including every declarator and cleanup. */
static int cleaned;
static void cleanup(int *p) { cleaned += *p; }
__attribute__((noinline)) static int direct(void) {
    goto begin;
begin:
    volatile int a = 3, b = 5;
    volatile int c = 7;
    return a + 2*b + 3*c;
}
__attribute__((noinline)) static int indirect(void) {
    static void *labels[] = {&&begin};
    goto *labels[0];
begin:
    volatile int a = 11;
    volatile int b = 13;
    return a + b;
}
__attribute__((noinline)) static int with_cleanup(void) {
    goto first;
first:
second:
    int a __attribute__((cleanup(cleanup))) = 17;
    if (cleaned) return 1;
    a = 19;
    return 0;
}
int main(void) {
    if (direct() != 34) return 1;
    if (indirect() != 24) return 2;
    if (with_cleanup() || cleaned != 19) return 3;
    return 0;
}
