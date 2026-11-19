/* [dcl.type.decltype]: a member template's trailing return type
   decltype(g(i)) names its template parameter, a dependent type
   ([temp.dep.type]/10.3), also inside the class's member-specification
   (libc++'s ranges::iter_move) */
int g(int v) { return v + 1; }
struct F {
    template <class I>
    auto a(I i) const noexcept(noexcept(g(i))) -> decltype(g(i))
    {
        return g(i);
    }
};
int main()
{
    F f;
    return f.a(41) - 42;
}
