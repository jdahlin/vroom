/* [dcl.init.aggr]/16: a class expression initializes a subaggregate when an
   implicit conversion sequence reaches it, else its braces are elided */
namespace std {
template <class T> class initializer_list {
    const T *first;
    decltype(sizeof 0) count;
public:
    initializer_list() : first(nullptr), count(0) {}
    const T *begin() const { return first; }
    const T *end() const { return first + count; }
};
}
struct E {
    int a, b;
};
struct ToInt {
    int v;
    operator int() const { return v; }
};
struct ToE {
    operator E() const { return E{40, 50}; }
};
struct Derived : E {};
struct Pair {
    E first;
    int last;
};
/* an initializer-list constructor of an aggregate that the class cannot
   reach is not viable: the move constructor is chosen */
struct Set {
    int count;
    Set() : count(0) {}
    Set(Set &&o) : count(o.count + 100) {}
    Set(std::initializer_list<E> l) : count(0)
    {
        for (const E &e : l)
            count += e.a;
    }
};
struct Queue {
    Set q;
    Queue(Set &&s) : q{static_cast<Set &&>(s)} {}
};
int main()
{
    ToInt i{7};
    Pair elided{i, 2, 3};
    if (elided.first.a != 7 || elided.first.b != 2 || elided.last != 3)
        return 1;
    Pair converted{ToE{}, 4};
    if (converted.first.a != 40 || converted.first.b != 50 || converted.last != 4)
        return 2;
    Derived d{};
    d.a = 5;
    d.b = 6;
    E list[] = {d, ToE{}};
    if (list[0].a != 5 || list[0].b != 6 || list[1].a != 40)
        return 3;
    Set s{E{1, 0}, E{2, 0}};
    Queue queue{static_cast<Set &&>(s)};
    if (queue.q.count != 103)
        return 4;
    return 0;
}
