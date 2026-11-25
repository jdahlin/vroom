/* [namespace.udecl]/4: using Base::operator= where Base's only assignment
   operators are its implicit ones brings in nothing the derived class's
   own implicit copy/move assignment does not hide.  libc++'s <variant>
   (__dtor, __move_constructor, ...: using __base_type::operator=) over a
   trivially assigned base (Stockfish's score.h, std::variant). */
template <class T> struct base {
    T index = 0;
};
template <class T> struct derived : base<T> {
    using base_type = base<T>;
    using base_type::operator=;
    T extra = 0;
};
struct plain {
    int v = 1;
};
struct over : plain {
    using plain::operator=;
};
int main()
{
    derived<int> a, b;
    b.index = 3;
    b.extra = 4;
    a = b;
    if (a.index != 3 || a.extra != 4)
        return 1;
    over x, y;
    y.v = 5;
    x = y;
    if (x.v != 5)
        return 2;
    return 0;
}
