/* [temp.mem]/1, Example 2 (the valid half): a member template of a constrained class template defined outside it with an equivalent template-head; the error half (g) is src/test/cxx/unsupported/temp-mem-concept-head-mismatch.cpp */
template <typename T> concept C1 = true;
template <typename T> concept C2 = sizeof(T) <= 4;
template <C1 T> struct S {
    template <C2 U> int f(U);
    template <C2 U> int g(U u) { return int(u) + 1; }
};
template <C1 T> template <C2 U> int S<T>::f(U u)
{
    return int(u) + sizeof(T);
}
int main()
{
    S<char> s;
    return s.f(1) - 2 + s.g('a') - 98;
}
