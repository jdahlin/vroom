// [temp.over.link]/6: a function template declared with one set of
// template-parameter names and defined with others is one template; its
// overloads with other heads and parameter types stay separate.
template <class T>
struct Wrap { T value; };

template <class A, class B>
int pick(A a, B b);

template <class X, class Y, bool = true>
int pick(X *x, Y y);

template <class First, class Second>
int pick(First first, Second second) {
    return static_cast<int>(first) + 2 * static_cast<int>(second);
}

template <class P, class Q, bool>
int pick(P *p, Q q) {
    return *p * 10 + static_cast<int>(q);
}

template <class A>
int pick(Wrap<A> a) {
    return a.value;
}

int main() {
    int v = 4;
    if (pick(1, 2) != 5)
        return 1;
    if (pick<int, int, true>(&v, 3) != 43)
        return 2;
    if (pick(Wrap<int>{7}) != 7)
        return 3;
    return 0;
}
