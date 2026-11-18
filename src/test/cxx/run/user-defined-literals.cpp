// user-defined literals: [over.literal], [lex.ext]
#include <stdio.h>

typedef unsigned long size_t;

unsigned long long operator""_k(unsigned long long n) { return n * 1000; }
int operator""_c(char c) { return c - 'a'; }
int operator""_len(const char *s, size_t n) { return int(n) + (s[0] == 'h'); }
long operator""_w(const char16_t *s, size_t n) { return long(n) * 10 + (s[0] == u'x'); }

namespace units {
int operator""_m(unsigned long long n) { return int(n) * 100; }
}

int main()
{
    unsigned long long total = 3_k + 0x10_k + 0b11_k + 1'000_k; // 3000 + 16000 + 3000 + 1000000
    total += 'c'_c + "hello"_len + u"xy"_w;                     // 2 + 6 + 21
    using namespace units;
    total += 7_m;                                               // 700
    printf("%llu\n", total);
    return int(total & 255);
}
