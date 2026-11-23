/* [dcl.init.list]/3.10, [dcl.init]/8: an enumeration list-initialized from no
   element is value-initialized, zero: a default member initializer = {}
   (the C++ front end's own sema.h), a variable, a direct-list-initialization */
enum class Op { none, plus };
enum Plain { a, b };
struct S {
    Op op = {};
    Plain p = {};
    int k = 3;
};

int main()
{
    S s;
    Op o = {};
    Plain q{};
    Op r{};
    if (s.op != Op::none || s.p != a || o != Op::none || q != a ||
        r != Op::none)
        return 1;
    return s.k - 3;
}
