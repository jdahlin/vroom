/* [basic.lookup.argdep]/3: argument-dependent lookup that finds only function
   templates, which deduction drops, adds to what unqualified lookup found */
namespace N
{
struct tag {
    int value;
};
template <class A, class B, class C, class D> void apply(A, B, C, D)
{
}
} // namespace N

static int applied;

static void apply(const N::tag &t, int n)
{
    applied = applied * 10 + t.value + n;
}

int main()
{
    N::tag t = {1};
    apply(t, 2);
    apply(t, 3);
    return applied != 34;
}
