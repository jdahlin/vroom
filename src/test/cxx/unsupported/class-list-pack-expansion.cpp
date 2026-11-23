/* A pack expansion among the initializer-clauses of a class's braced list
   ([temp.variadic]/7): only an array's clauses are expanded, a class
   aggregate's or constructor's are not. */
struct S {
    int a, b, c;
};
template <class... Ts> constexpr int second()
{
    S v = {sizeof(Ts)...};
    return v.b;
}
int main()
{
    return second<int, char, long>();
}
