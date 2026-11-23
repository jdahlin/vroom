// [class.mem]/6: a constructor of a class template initializes members
// declared after it; [except.spec]/3: throw() is accepted in C++20; a local
// variable initialized from this.
template <class T> struct Late {
    Late() : a(40), b(2) {}
    T get() { return a + b; }
    T a;
    int b;
};

int plain(int v) throw() { return v; }

struct Owner;
struct Counter {
    int n;
    Counter(Owner *self, int add);
};

struct Owner {
    int n = 5;
    int f()
    {
        Counter c(this, 3);
        return c.n;
    }
};

Counter::Counter(Owner *self, int add) : n(add) { n += self->n; }

int main()
{
    Late<int> l;
    Owner o;
    return l.get() + plain(0) + o.f() - 50;
}
