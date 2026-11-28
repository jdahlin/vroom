/* [abi:expressions]: a unary fold in a requires-clause (std::variant's
   operator<=>) */
template <class T> concept C = sizeof(T) > 0;
template <class... T> struct V {};
template <class... T> requires(C<T> && ...) int f(const V<T...> &) { return 0; }
int main() { V<int, long> a; return f(a); }
template <class... T>
    requires(... || C<T>)
int g(const V<T...> &)
{
    return 0;
}
int h()
{
    V<int> a;
    return g(a);
}
