/* [abi:mangle.decltype] of a decltype written in an alias template, used in
   another template's declaration: the alias's parameters name the types the
   use binds them to, a pack's fold expanded over its elements ([temp.alias]/2,
   [temp.variadic]/4). */
float pi(float) { return 1; }
double pi(int) { return 2; }

template <class A>
using one = decltype(pi(A()));

template <class... A>
using sum = decltype((pi(A()) + ...));

template <class B>
one<B> first(B) { return 0; }

template <class B, class C>
sum<B, C> total(B, C) { return 0; }

int main()
{
    double a = first(1);
    double b = total(1, 2);
    if (a != 0 || b != 0)
        return 1;
    return 0;
}
