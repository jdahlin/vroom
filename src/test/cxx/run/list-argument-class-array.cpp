/* [over.ics.list]/6: a braced list of class elements to a reference to an
   array; [over.ics.ref]/2: a bit-field argument to a const reference among
   overloaded templates */
template <class T> struct pair2 { T e[2]; };
struct M {
    int a, b;
};
template <class T> pair2<T> make(T (&a)[2]) { return {{a[0], a[1]}}; }
template <class T> pair2<T> make(T (&&a)[2]) { return {{a[1], a[0]}}; }
int pick(M (&)[2]) { return 1; }
int pick(M (&&)[2]) { return 2; }

/* [over.ics.list]/2: {d} to Derived is the identity, better than the
   derived-to-base conversion of the array element */
struct U {
    int u = 1;
};
struct Derived : U {};
int take(Derived) { return 1; }
int take(U (&&)[1]) { return 2; }

template <class T> const T &least(const T &a, const T &b)
{
    if (b < a)
        return b;
    return a;
}
template <class T> int least(T *) { return 100; }

struct P {
    int x : 24;
    int y : 8;
    int clamp(int n) const
    {
        int o = least(x, n);
        return o;
    }
};

int main()
{
    pair2<M> g = make<M>({M{1, 2}, M{3, 4}});
    if (g.e[0].a != 3 || g.e[1].b != 2)
        return 1;
    if (pick({M{1, 1}, M{2, 3}}) != 2)
        return 2;
    if (pick({{1, 1}, {2, 3}}) != 2)
        return 3;
    Derived d;
    if (take({d}) != 1)
        return 5;
    P p{5, 1};
    if (p.clamp(7) != 5 || p.clamp(-3) != -3)
        return 4;
    return 0;
}
