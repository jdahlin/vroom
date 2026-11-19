// [temp.deduct.call]/4.3: a parameter of a class template's
// specialization deduced from a derived class of one; deduction fails
// for an int or a class without such a base, which removes the template
// from the candidates (as libc++'s is_eq(partial_ordering) meets its
// operator== templates).
template <class T> struct box {
    T value;
};
struct derived : box<int> {};
struct other {
    int value;
};
template <class T> int pick(const box<T> &b) { return b.value + 1; }
template <class T> int ask(box<T>) { return 1; }
int ask(other) { return 2; }
int ask(long) { return 3; }
// A direct match that fails partway (T = long, then short against int)
// leaves no value for the base's match: T is char.
template <class T, class U> struct two {
    T first;
    U second;
};
template <> struct two<long, short> : two<char, int> {};
template <class T> int size(const two<T, int> &) { return sizeof(T); }
int main()
{
    derived d;
    d.value = 4;
    other o{0};
    two<long, short> t;
    return pick(d) == 5 && ask(o) == 2 && ask(0) == 3 && size(t) == 1
               ? 0
               : 1;
}
