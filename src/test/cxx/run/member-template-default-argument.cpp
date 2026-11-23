/* [class.mem]/6: a default template-argument is no complete-class context;
   a member template's (libc++'s error_condition(_Ep): enable_if in a
   default argument) is read with the declaration, not skipped as one of a
   function parameter */
namespace a
{
int f(int v) { return v; }
} // namespace a
template <class T> struct is_int { static const bool value = false; };
template <> struct is_int<int> { static const bool value = true; };
struct C {
    template <class E, bool = is_int<E>::value, int N = 2> int h(E e)
    {
        using a::f;
        return f(e) + N;
    }
    template <class E, int N = 3> int g(E e) { return e + N; }
};
int main()
{
    C c;
    return c.h(37) + c.g(0) - 42;
}
