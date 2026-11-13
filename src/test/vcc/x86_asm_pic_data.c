/* vcc-flags: -fPIC */
#if defined(__x86_64__)
static const int table[] = { 11, 22 };
static volatile int index_value = 1;

static int load(void)
{
    const int *address;
    int index;
    __asm__("movq %1, %0" : "=r"(address) : "r"(table));
    __asm__("movl %1, %0" : "=r"(index) : "r"(index_value));
    return address[index];
}
#endif

int main(void)
{
#if defined(__x86_64__)
    return load() != 22;
#else
    return 0;
#endif
}
