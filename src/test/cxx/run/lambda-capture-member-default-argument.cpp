/* a lambda in the default argument of a member function: its closure type
   is mangled in the function's default-argument scope ([abi:closure-types],
   _ZZN1S1gEiEd_NKUlvE_clEv) */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int zero = 0;

struct S {
    int g(int d = [] { return 3; }()) { return d; }
};

int main()
{
    S s;
    check(s.g() + zero == 3 && s.g(4) == 4);
    return 0;
}
