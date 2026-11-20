/* a lambda in the default argument of a member function: its closure type
   is mangled in the function's default-argument scope ([abi:closure-types],
   _ZZN1S1gEiEd_NKUlvE_clEv) */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int zero = 0;

struct S {
    int g(int d = [] { return 3; }()) { return d; }
    int two(int a = [] { return 1; }(), int b = [] { return 2; }()) { return a * 10 + b; }
    int h(int a, int d = [] { return 4; }()) { return a + d; }
};

inline int free_inline(int d = [] { return 5; }()) { return d; }

int main()
{
    S s;
    check(s.g() + zero == 3 && s.g(4) == 4);
    check(s.h(1) + zero == 5 && free_inline() + zero == 5 &&
          s.two() + zero == 12);
    return 0;
}
