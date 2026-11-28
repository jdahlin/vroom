/* [expr.type.conv]/2: T{} and T() of a pointer to member type is its null
   value (default_initializable<int S::*>, views::transform(&S::m)) */
struct S {
    int a;
    int f()
    {
        return a;
    }
};
template <class T>
concept braced = requires { T{}; };
template <class T> bool null()
{
    T x{};
    return x == nullptr && T{} == nullptr && T() == nullptr;
}
using data = int S::*;
using function = int (S::*)();
static_assert(braced<data> && braced<function>);
int main()
{
    return !null<data>() || !null<function>() || data{} != nullptr ||
           function() != nullptr;
}
