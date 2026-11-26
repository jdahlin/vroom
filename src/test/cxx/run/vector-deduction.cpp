/* [temp.deduct.type]/8, [ext:clang_ext_vector_type]: N and T of a function
   parameter of vector type deduced from the argument's vector */
template <int N, typename T> struct Vec { T v[N]; };
template <int N, typename T> using VExt = T __attribute__((ext_vector_type(N)));
template <int N, typename T> static VExt<N, T> to_vext(const Vec<N, T> &v)
{
    VExt<N, T> r;
    __builtin_memcpy(&r, &v, sizeof r);
    return r;
}
template <int N, typename T> static Vec<N, T> to_vec(const VExt<N, T> &v)
{
    Vec<N, T> r;
    __builtin_memcpy(&r, &v, sizeof r);
    return r;
}
template <typename D, int N, typename S> Vec<N, D> cast(const Vec<N, S> &src)
{
    return to_vec(__builtin_convertvector(to_vext(src), VExt<N, D>));
}
template <int N, typename T> int by_value(VExt<N, T>)
{
    return N * 100 + static_cast<int>(sizeof(T));
}
int main()
{
    Vec<4, int> a{{1, 2, 3, 4}};
    Vec<4, float> f = cast<float>(a);
    if (f.v[0] != 1 || f.v[3] != 4)
        return 1;
    VExt<8, short> s = {};
    if (by_value(s) != 802)
        return 2;
    return to_vec(to_vext(a)).v[2] != 3;
}
