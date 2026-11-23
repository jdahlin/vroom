/* [stmt.select]: P2360R0 (C++23) lets an init-statement be an
   alias-declaration; clang accepts it before C++23 as an extension
   ([ext:clang_later_edition_features]).  libc++'s __for_each uses it. */
template <class T>
int size_of()
{
    if constexpr (using U = T; sizeof(U) == 4)
        return 4;
    else
        return 8;
}
int main()
{
    int r = 0;
    if (using I = long; sizeof(I) == 8)
        r += 1;
    switch (using C = char; sizeof(C)) {
    case 1:
        r += 1;
        break;
    }
    for (using N = int; N i = 0; i++)
        r += 0;
    return size_of<int>() + size_of<long>() - 12 + r - 2;
}
