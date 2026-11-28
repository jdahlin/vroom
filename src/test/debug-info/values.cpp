/* bench/debug-values.py: C++ parameters, this, references, bases and
   members print as from clang's program */
namespace ns
{
struct Base {
    int b;
    virtual int v() { return b; }
};
int hidden = 6;
} // namespace ns

struct S : ns::Base {
    int m;
    int &r;
    S(int &x) : m(2), r(x) { b = 1; }
};

template <class T> struct Box {
    T item;
};

int total = 5;

__attribute__((noinline)) static int use(const S &s, Box<long> box, S *p,
                                         int &&moved)
{
    int x = s.m + (int)box.item;
    const int &alias = x;

    return x + total + ns::hidden + p->b + moved + alias; /* STOP */
}

int main()
{
    int v = 7;
    S s(v);
    Box<long> box{9};

    return use(s, box, &s, 4) == 0;
}
/* PRINT s.m s.b s.r box.item p->m p->b moved x alias total */
