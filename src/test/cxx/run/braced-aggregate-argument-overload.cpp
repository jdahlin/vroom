/* [over.ics.rank]/3.3: two references to one aggregate initialized from a
   braced list are told apart by the second standard conversion: the rvalue
   reference is better ([over.ics.rank]/3.2.3). */
struct A {
    int x;
    const char *s;
};
static int f(const A &) { return 1; }
static int f(A &&a) { return 2 + a.x; }

int main()
{
    const char *s = "";
    if (f({3, s}) != 5)
        return 1;
    A a{1, s};
    if (f(a) != 1)
        return 2;
    return 0;
}
