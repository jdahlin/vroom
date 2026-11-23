/* [temp.mem]/1, [temp.pre]/2: a member variable template of a class template */
template <class P> struct H {
    template <class T> static constexpr int both = sizeof(P) + sizeof(T);
};
static_assert(H<char>::both<long> == 9, "");
int main()
{
    return H<int>::both<char> - 5;
}
