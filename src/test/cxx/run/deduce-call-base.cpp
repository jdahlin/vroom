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
int main()
{
    derived d;
    d.value = 4;
    other o{0};
    return pick(d) == 5 && ask(o) == 2 && ask(0) == 3 ? 0 : 1;
}
