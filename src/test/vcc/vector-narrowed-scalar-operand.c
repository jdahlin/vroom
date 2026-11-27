/* a scalar converted to a vector's char or short element type beside the
   vector: the lanes see the truncated value (arm64's lane by lane path
   used the register's upper bits) */
#include <stdio.h>

typedef unsigned char C __attribute__((ext_vector_type(4)));
typedef short S __attribute__((ext_vector_type(4)));

__attribute__((noinline)) static void run(long wide, unsigned long long big)
{
    C c = {10, 100, 200, 255};
    C q = c / wide;
    S s = {-100, 0, 100, 30000};
    S t = s / big;
    typedef char M __attribute__((ext_vector_type(4)));
    M less = c < wide;
    printf("%d %d %d %d\n", q[0], q[1], q[2], q[3]);
    printf("%d %d %d %d\n", t[0], t[1], t[2], t[3]);
    printf("%d %d\n", less[0], less[3]);
}

int main(int argc, char **argv)
{
    run(256 + 3 * argc, 0x10000ull + 40000u * argc);
    return 0;
}
