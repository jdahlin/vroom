/* [over.ics.list]/5: a string-literal converts to no char element, so
   {""} calls the const char* overload, not the initializer_list one
   (std::string{""}) */
namespace std
{
template <class E>
struct initializer_list {
    const E *first;
    decltype(sizeof 0) count;
};
}
struct S {
    int n;
    S(std::initializer_list<char> l)
        : n(100 + (int)l.count)
    {
    }
    S(const char *)
        : n(1)
    {
    }
};
int f(std::initializer_list<char> l)
{
    return 100 + (int)l.count;
}
int f(const char *)
{
    return 2;
}
int main()
{
    S a{""};
    S b{"ab"};
    S c = {""};
    return a.n + b.n + c.n + S{""}.n + f({""}) + S({""}).n - 7;
}
