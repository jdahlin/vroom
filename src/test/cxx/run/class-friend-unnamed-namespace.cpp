/* [class.friend]/6, [basic.link]/4: a function first declared by a friend
   declaration in a class of an unnamed namespace, with a parameter of that
   class (a type without external linkage): it has internal linkage, and
   argument-dependent lookup finds it (unit-regression1's to_json) */
namespace lib
{
struct J {
    int v;
};
} // namespace lib
namespace
{
struct nocopy {
    int val;
    friend void to_json(lib::J &j, const nocopy &n)
    {
        j.v = n.val;
    }
};
} // namespace

int main()
{
    nocopy n{7};
    lib::J j{0};
    to_json(j, n);
    if (j.v != 7)
        return 1;
    return 0;
}
