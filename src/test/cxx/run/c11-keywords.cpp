/* [ext:clang_c11_keywords] _Static_assert, _Alignas and _Alignof in C++:
   other spellings of static_assert, alignas and alignof (clang:
   -Wc11-extensions), as vcc's own vir.h, ccint.h, intern.cpp and
   header-cache-internal.h use them */
typedef unsigned long uint64_t;
typedef struct chunk {
    _Alignas(16) uint64_t h;
    int n;
} chunk;
_Static_assert(sizeof(chunk) == 16, "chunk is 16 bytes");
_Static_assert(_Alignof(chunk) == 16);
struct wide {
    _Static_assert(sizeof(int) == 4, "member scope");
    _Alignas(int) char c;
    _Alignas(32) char d;
};
template <class T> struct sized {
    _Static_assert(sizeof(T) == 4, "template member scope");
    static const int value = sizeof(T);
};
namespace ns {
_Static_assert(_Alignof(long) == 8, "namespace scope");
}
int main()
{
    _Static_assert(sizeof(char) == 1, "block scope");
    return _Alignof(wide) + sized<int>::value + sizeof(wide) + _Alignof(char) -
           32 - 4 - 64 - 1;
}
