/* The constant evaluator on pointers to single objects and static
   storage: assignment and compound assignment of a pointer ([expr.ass],
   [expr.add]/4), a single object as an array of one element
   ([basic.compound]/3.3), the address of an element, constexpr pointer
   variables ([expr.const]/13: static storage, string literals), reads
   through them ([expr.const]/5.8). */
#include <stdio.h>

constexpr int a[3] = {1, 2, 3};
constexpr const int *second = a + 1;
constexpr int v = 3;
constexpr const int *pv = &v;
constexpr const char *word = "abc";
int mutable_global = 5;
constexpr int *pg = &mutable_global;

constexpr int walk()
{
    int b[4] = {1, 2, 3, 4};
    int *p = b;
    p += 2;
    p = p + 1;
    p -= 2;
    return *p;
}

constexpr int one_past()
{
    int x = 7;
    int *p = &x;
    int *e = p + 1;
    return (e == p + 1) + (p != e) + *(e - 1);
}

constexpr int element_address()
{
    int b[4] = {1, 2, 3, 4};
    int *p = &b[1];
    p += 2;
    return *p + static_cast<int>(&b[3] - &b[0]);
}

constexpr int length(const char *s)
{
    int n = 0;
    while (s[n])
        ++n;
    return n;
}

constexpr void bump(int *p) { ++*p; }

constexpr int through_argument()
{
    int x = 1;
    bump(&x);
    bump(&x);
    return x;
}

constexpr int read(const int *p) { return *p; }

static_assert(walk() == 2);
static_assert(one_past() == 9);
static_assert(element_address() == 7);
static_assert(*second == 2 && second[1] == 3);
static_assert(*pv == 3);
static_assert(word[1] == 'b' && word[3] == 0);
static_assert(length(word) == 3);
static_assert(length("hello") == 5);
static_assert(through_argument() == 3);
static_assert(read(&v) == 3 && read(second) == 2);
static_assert(pg != nullptr);

int main()
{
    printf("%d %d %d %d %d %d\n", walk(), one_past(), element_address(),
           length(word), through_argument(), read(pv) + *pg);
    return 0;
}
