/* [expr.prim.lambda.closure]/3: lambdas with a template-parameter-list,
   whose function call operator is a member function template: called
   with arguments of several types, constrained by a requires-clause, at
   namespace scope (in an inline variable too) and in a function;
   constexpr ones ([expr.prim.lambda.closure]/4) in constant expressions */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

int zero = 0;

auto twice = []<class T>(T a) { return a * 2; };
inline auto less = []<class T, class U>(const T &a, const U &b) {
    return a < b;
};

template <class T> concept small = sizeof(T) <= 4;

/* the overload a requires-clause allows: after the template-parameters
   and after the parameters */
inline auto pick = []<class T>
    requires small<T>
(T a) { return a + 1; };
auto ordered = []<class T, class U>(const T &a, const U &b)
    requires small<U>
{ return b < a; };

inline constexpr auto square = [](int x) constexpr { return x * x; };
static_assert(square(3) == 9);
inline constexpr auto sum = []<class T>(T x, T y) constexpr { return x + y; };
static_assert(sum(4, 5) == 9);

int main()
{
    check(twice(zero + 3) == 6);
    check(twice(zero + 1.5) == 3.0);
    check(less(zero + 1, 2.5) && !less(2.5, zero + 1));
    auto add = []<class T>(T a, int b) -> long { return a + b; };
    check(add(zero + 1, 2) == 3 && add('a', zero + 1) == 'b');
    check(pick(zero + 1) == 2 && pick(char(zero + 'a')) == 'b');
    check(ordered(2.5, zero + 1));
    check(square(zero + 4) == 16 && sum(zero + 1, 2) == 3);
    return 0;
}
