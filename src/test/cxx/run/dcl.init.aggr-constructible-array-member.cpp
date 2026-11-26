/* [dcl.init]/17.6.2.2: a parenthesized list elides no braces and an array
   is not initialized by an expression, so an aggregate's array member
   takes no argument (std::array, from nlohmann/json) unless a conversion
   function gives the aggregate; before C++20 no aggregate is
   paren-initialized */
struct Array {
    int a[2];
};
typedef int Two[2];
struct ToInt {
    operator int() const;
};
struct ToArray {
    operator Array() const
    {
        return Array{{1, 2}};
    }
};
struct Scalar {
    int x;
};
static_assert(!__is_constructible(Array, int), "");
static_assert(!__is_constructible(Array, int *), "");
static_assert(!__is_constructible(Array, Two &), "");
static_assert(!__is_constructible(Array, Two &&), "");
static_assert(!__is_constructible(Array, ToInt), "");
static_assert(__is_constructible(Array, ToArray), "");
static_assert(__is_constructible(Scalar, int) == (__cplusplus > 201703L), "");
int main()
{
    ToArray from;
    Array made(from);
    return made.a[1] != 2;
}
