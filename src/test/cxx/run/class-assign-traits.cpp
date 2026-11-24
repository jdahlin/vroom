/* The implicit copy assignment operator when it is deleted: a const member
   ([class.copy.assign]/7.2) or a declared move assignment operator without a
   copy assignment operator (/2). */
struct C { const int c; };
struct M { int v; M &operator=(M &&o) { v = o.v + 1; return *this; } };
struct P { int a; };

static_assert(!__is_assignable(C &, C &&));
static_assert(!__is_assignable(C &, const C &));
static_assert(!__is_trivially_assignable(C &, C &&));
static_assert(__is_assignable(M &, M &&));
static_assert(!__is_assignable(M &, M &));
static_assert(!__is_assignable(M &, const M &));
static_assert(__is_assignable(P &, const P &));

int main()
{
    M a{1}, b{5};
    a = static_cast<M &&>(b);
    P p{1}, q{2};
    p = q;
    if (a.v != 6 || p.a != 2)
        return 1;
    return 0;
}
