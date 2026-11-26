// [class.friend]: a friend function declaration whose return type is an
// elaborated-type-specifier (e.g. `friend class Reader &operator>>(class Reader &, Order &);`).

struct Order {
private:
    int val;
    friend class Reader &operator>>(class Reader &r, Order &o);

public:
    Order() : val(0) {}
    int get() const { return val; }
};

class Reader {
public:
    int next;
    Reader(int n) : next(n) {}
};

Reader &operator>>(Reader &r, Order &o)
{
    o.val = r.next;
    return r;
}

int main()
{
    Reader r(42);
    Order o;
    r >> o;
    if (o.get() != 42)
        return 1;
    return 0;
}
