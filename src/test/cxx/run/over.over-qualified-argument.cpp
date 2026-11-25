/* [over.over]/1: a qualified name of a function template as an argument:
   the target type of each candidate selects from the qualified name's set */
namespace N
{
struct S {
    int v = 0;
};
S &bump(S &s);
template <class T> T &twice(T &t)
{
    t.v += 2;
    return t;
}
} // namespace N
struct Base {};
struct O : Base {
    int n = 0;
    O &operator<<(O &(*f)(O &))
    {
        return f(*this);
    }
    O &operator<<(Base &(*)(Base *))
    {
        n = 100;
        return *this;
    }
    O &operator<<(void (*)(int))
    {
        n = 200;
        return *this;
    }
    int v = 0;
};
int main()
{
    O o;
    o << N::twice << N::twice;
    return !(o.v == 4 && o.n == 0);
}
