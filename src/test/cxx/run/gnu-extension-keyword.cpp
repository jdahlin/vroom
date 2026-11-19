/* [ext:gnu_extension_keyword]: __extension__ before a declaration at
   namespace, class and block scope (glibc's and Linux's <asm/types.h>),
   and before an expression; it changes no meaning */
__extension__ typedef __signed__ long long s64;
__extension__ typedef unsigned long long u64;

__extension__ static int counter = 2;

struct Pair {
    __extension__ long long a;
    __extension__ union {
        int b;
        float f;
    };
};

extern "C" {
__extension__ typedef int c_int;
}

static int twice(int x)
{
    __extension__ int y = x * 2;
    return __extension__ y;
}

int main()
{
    int bad = 0;
    if (sizeof(s64) != 8 || sizeof(u64) != 8 || (s64)-1 >= 0) bad |= 1;
    Pair p;
    p.a = 5;
    p.b = 7;
    if (p.a + p.b != 12) bad |= 2;
    c_int c = twice(counter);
    if (c != 4) bad |= 4;
    if (__extension__(1 + 2) != 3) bad |= 8;
    return bad;
}
