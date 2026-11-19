// [ext:clang_blocks] ^ as a ptr-operator: a block pointer to a function
// type, as the Darwin SDK's <stdlib.h> declares atexit_b and qsort_b under
// __BLOCKS__.  A function taking one is mangled with clang's vendor
// qualifier U13block_pointer (conversions of a
// block pointer are not analysed yet); ^ is still exclusive or in an expression.
/* cxx-run-flags: -fblocks */
extern "C" int run_b(int (^_Nonnull)(int));
typedef void (^handler)(void);
int count(handler, int (^const)(int)) { return 2; }
int main()
{
    int a = 6, b = 3;
    if ((a ^ b) != 5) return 1;
    if (sizeof(handler) != sizeof(void *)) return 3;
    return 0;
}
