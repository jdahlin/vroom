// clang's __attribute__((overloadable)) inside extern "C" (as in glibc's
// _FORTIFY_SOURCE bits/fcntl2.h): allows overloads of a C-linkage name and
// mangles the overloadable declarations.

extern "C" {
int overloadable_c_fn(int a) { return a + 10; }

__attribute__((__overloadable__))
int overloadable_c_fn(int a, int b) {
    return a * 10 + b;
}

extern __inline __attribute__((__always_inline__, __gnu_inline__, __overloadable__))
int overloadable_c_fn(int a, int b, int c) {
    return a * 100 + b * 10 + c;
}
}

int main() {
    if (overloadable_c_fn(5) != 15)
        return 1;
    if (overloadable_c_fn(2, 3) != 23)
        return 2;
    if (overloadable_c_fn(1, 2, 3) != 123)
        return 3;
    return 0;
}
