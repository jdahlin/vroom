// [diff.cpp14.dcl.dcl]/1: register, a storage class of block variables and
// parameters until C++17.
/* cxx-run-flags: -std=c++14 */
typedef int T;
static int add(register int a, register T b)
{
    register T sum = a + b;
    return sum;
}

int main()
{
    int total = 0;
    for (register int i = 0; i < 4; i++)
        total += i;
    register auto *p = &total;
    int (*f)(int, int) = add;
    return f(*p, 1) == 7 ? 0 : 1;
}
