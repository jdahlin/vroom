/* [class.friend]/6: a friend a class template specialization defines, with a
   local type in its signature (libc++'s iter_move of an iterator over one);
   [impl.clang] its symbol is local. */
extern "C" int printf(const char *, ...);
template<class T> struct It {
    T *p;
    friend T &&move_(const It &i) { return static_cast<T &&>(*i.p); }
    friend T get_(It i) { return *i.p; }
};
static int local()
{
    enum E { A = 7 };
    E e = A;
    It<E> i{&e};
    return move_(i) + get_(i);
}
int main() { printf("%d\n", local()); return local() != 14; }
