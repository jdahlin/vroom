/* abi-targets: linux; x86-64 array object alignment, ARM interoperability */
extern int values[10];
extern char bytes[17], zeroes[17], nested[2][8];
extern char explicit_small[17] __attribute__((aligned(1)));
extern const char readonly_bytes[17];
extern char *static_array(void);
extern int local_array(void);
extern int tls_array(void);
_Static_assert(__alignof__(bytes) == 1, "ABI object alignment is not type alignment");

int check_pointer(void *p)
{
#if defined(__x86_64__)
    return ((unsigned long)p & 15) != 0;
#else
    return p == 0;
#endif
}
int main(void)
{
    /* Clang may use aligned SIMD loads here, just as for external arrays in
       ordinary application code. No integer-to-pointer extension is needed. */
    for (int i=0;i<10;i++) if(values[i] != 100+4*i) return 1;
    if(check_pointer(values) || check_pointer(bytes) || check_pointer(zeroes) ||
       check_pointer((void *)readonly_bytes) || check_pointer(nested) ||
       check_pointer(static_array()) || local_array() || tls_array()) return 2;
    if(bytes[0]!=1 || readonly_bytes[0]!=2 || nested[0][0]!=4 || explicit_small[0]!=5) return 3;
    return 0;
}
