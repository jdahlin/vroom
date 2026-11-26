/* [expr.new], [dcl.init]/17.6.1: a new-initializer that is a prvalue of the
   class (a call, a conditional of calls) initializes the allocated object
   itself: no copy, no move */

static int copies;

struct self {
    self *where;
    int value;
    explicit self(int v) : where(this), value(v) {}
    self(const self &o) : where(this), value(o.value) { ++copies; }
    ~self() {}
};

struct small {
    int a, b;
};

struct large {
    long a[5];
};

struct maker {
    self make(int v) const { return self(v); }
    operator small() const { return {3, 4}; }
};

static self make(int v, int w = 0) { return self(v + w); }
static small pair() { return {1, 2}; }
static large five() { return {{1, 2, 3, 4, 5}}; }

int main(int argc, char **)
{
    maker m;
    self *a = new self(make(1));
    self *b = new self(m.make(2));
    self *c = new self(argc > 0 ? make(3, argc) : make(9));
    small *d = new small(pair());
    small *e = new small(static_cast<small>(m));
    large *f = new large(five());
    if (a->where != a || b->where != b || c->where != c || copies)
        return 1;
    if (a->value != 1 || b->value != 2 || c->value != 4)
        return 2;
    if (d->a + d->b != 3 || e->a + e->b != 7 || f->a[4] != 5)
        return 3;
    delete a;
    delete b;
    delete c;
    delete d;
    delete e;
    delete f;
    return 0;
}
