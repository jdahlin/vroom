/* [temp.deduct.call]: a function argument never matches a template-id
   parameter, nor does a T* whose T is already another type a class pointer:
   such a template is no candidate */
template <class C> struct Str {};
struct Base {
    int flags = 0;
};
Base &fixed(Base &b)
{
    b.flags = 1;
    return b;
}
struct Out : Base {};
template <class C> int operator<<(Out &, const Str<C> &)
{
    return 100;
}
template <class C> int operator<<(Out &, Str<C> *)
{
    return 200;
}
template <class C> int operator<<(Out &, const C *)
{
    return 300;
}
int operator<<(Out &o, Base &(*f)(Base &))
{
    f(o);
    return o.flags;
}
template <class C> int say(C, const C *)
{
    return 1;
}
int say(char, ...)
{
    return 2;
}
int main()
{
    Out o;
    if (say('x', &o) != 2)
        return 2;
    return (o << fixed) != 1;
}
