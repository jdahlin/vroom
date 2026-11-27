/* [temp.variadic]/7, [ext:gnu_vector_size]: __builtin_shufflevector's
   indices as a pack expansion */
typedef int v4 __attribute__((ext_vector_type(4)));
typedef int v2 __attribute__((ext_vector_type(2)));
template <int... Ix> v2 sh(v4 x)
{
    return __builtin_shufflevector(x, x, Ix...);
}
int main()
{
    v4 x = {3, 4, 5, 6};
    v2 v = sh<1, 0>(x);
    return v[0] != 4 || v[1] != 3;
}
