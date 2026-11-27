/*
 * Out-of-class definitions of overloaded member functions of a class template
 * with the same name and parameter count: instantiating one overload must not
 * instantiate the body of an unused sibling overload ([temp.inst]/2, /4).
 */

struct MoveOnly {
    int value;
    MoveOnly(int v) : value(v) {}
    MoveOnly(const MoveOnly &) = delete;
    MoveOnly(MoveOnly &&other) : value(other.value) { other.value = -1; }
};

struct Copyable {
    int value;
    Copyable(int v) : value(v) {}
    Copyable(const Copyable &other) : value(other.value + 100) {}
    Copyable(Copyable &&other) : value(other.value + 10) { other.value = -1; }
};

template <typename T> struct Box {
    int last = 0;
    void push(const T &x);
    void push(T &&x);
};

template <typename T> void Box<T>::push(const T &x)
{
    T copy(x);
    last = copy.value;
}

template <typename T> void Box<T>::push(T &&x)
{
    T moved(static_cast<T &&>(x));
    last = moved.value;
}

int main()
{
    Box<MoveOnly> bm;
    MoveOnly m(42);
    bm.push(static_cast<MoveOnly &&>(m));
    if (bm.last != 42 || m.value != -1)
        return 1;

    Box<Copyable> bc;
    Copyable c1(5);
    bc.push(static_cast<Copyable &&>(c1));
    if (bc.last != 15 || c1.value != -1)
        return 2;

    Copyable c2(7);
    bc.push(c2);
    if (bc.last != 107 || c2.value != 7)
        return 3;

    return 0;
}
