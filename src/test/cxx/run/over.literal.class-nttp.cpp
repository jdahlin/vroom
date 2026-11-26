// [over.literal]/5: a C++20 string literal operator template with a single
// non-type template parameter of class or deduced class template type.
template <int N>
struct fixed_string {
    char data[N];
};

template <fixed_string S>
constexpr int operator""_a()
{
    return sizeof(S.data);
}

int main()
{
    return 0;
}
