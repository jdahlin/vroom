/* [over.match.viable]/4, [class.copy.assign]/2: a class that declares an
   operator= taking another type: the implicit copy assignment operator is no
   candidate for a right operand it cannot convert from */
struct Sink {
    int *p;
    int last;
    Sink(int *q) : p(q), last(0) {}
    Sink &operator=(char c)
    {
        last = c;
        *p = c;
        return *this;
    }
    Sink &operator=(long l)
    {
        last = static_cast<int>(l) + 1;
        return *this;
    }
};

struct Pair {
    int a;
    Pair(int *q) : a(*q) {}
    Pair &operator=(double d)
    {
        a = static_cast<int>(d);
        return *this;
    }
};

int main()
{
    int cell = 0;
    Sink s(&cell);
    s = 'a';
    if (cell != 97 || s.last != 97)
        return 1;
    s = 10L;
    if (s.last != 11)
        return 2;
    Pair p(&cell);
    p = 2.5;
    if (p.a != 2)
        return 3;
    return 0;
}
