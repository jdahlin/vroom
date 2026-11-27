/* [temp.func.order]: templates with a placeholder non-type parameter in
   their types are ordered by the types around it (Skia's sk_ref_sp on a
   raw_ptr<const T, Traits>). */
extern "C" int printf(const char *, ...);

template <class T, auto Traits> struct raw_ptr {};

template <class T, auto Traits> int ref(const raw_ptr<T, Traits> &) { return 1; }
template <class T, auto Traits> int ref(const raw_ptr<const T, Traits> &)
{
    return 2;
}

int main()
{
    raw_ptr<const int, 0> c;
    raw_ptr<int, 1u> m;
    printf("%d %d\n", ref(c), ref(m));
    return 0;
}
