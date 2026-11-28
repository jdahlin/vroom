/* [func.require]/1.2, 1.5: INVOKE of a pointer to member on a
   std::reference_wrapper is on what it refers to */
namespace std
{
template <class T> struct reference_wrapper {
    T *p;
    __attribute__((always_inline)) operator T &() const { return *p; }
    __attribute__((always_inline)) T &get() const { return *p; }
};
} // namespace std
struct S {
    int v;
    int get() const { return v; }
};
int main()
{
    S s{3};
    std::reference_wrapper<S> r{&s};
    return __builtin_invoke(&S::get, r) + __builtin_invoke(&S::v, r) - 6;
}
