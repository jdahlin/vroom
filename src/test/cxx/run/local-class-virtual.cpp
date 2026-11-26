/* [class.local], [class.virtual]: local classes with virtual functions, in
   an ordinary, an inline and a template function: their members, virtual
   tables and type information have <local-name> symbols
   ([abi:mangling-scope]) */
struct Base {
    virtual int point() const
    {
        return 1;
    }
    virtual int sep() const
    {
        return 2;
    }
    virtual ~Base() {}
};

static int destroyed;

static int use(const Base &b)
{
    return b.point() * 10 + b.sep();
}

static int plain()
{
    class Comma : public Base {
      protected:
        int point() const override
        {
            return 3;
        }

      public:
        ~Comma() override
        {
            destroyed++;
        }
    };
    Base *p = new Comma;
    int r = use(*p);
    delete p;
    return r;
}

inline int in_inline(int k)
{
    struct Local : Base {
        int k;
        Local(int a) : k(a) {}
        int sep() const override
        {
            return k;
        }
    };
    Local l(k);
    return use(l);
}

static inline int in_static_inline()
{
    struct Local : Base {
    };
    Local l;
    return use(l);
}

namespace
{
inline int in_unnamed()
{
    struct Local : Base {
    };
    Local l;
    return use(l);
}
} // namespace

struct Second {
    int pad = 0;
    virtual int two()
    {
        return 2;
    }
    virtual ~Second() {}
};

inline int thunks()
{
    struct Both : Base, Second {
        int two() override
        {
            return 20;
        }
    };
    struct Shared : virtual Second {
    };
    struct Diamond : Shared {
        int two() override
        {
            return 30;
        }
    };
    Both b;
    Diamond d;
    Second *p = &b, *q = &d;
    return p->two() + q->two();
}

template <int N> int in_template()
{
    struct Local {
        virtual int get()
        {
            return N;
        }
    };
    struct More : Local {
        int get() override
        {
            return N + 1;
        }
    };
    More m;
    Local *p = &m;
    return p->get();
}

int main()
{
    if (plain() != 32 || destroyed != 1)
        return 1;
    if (in_inline(7) != 17)
        return 2;
    if (in_static_inline() != 12 || in_unnamed() != 12)
        return 5;
    if (thunks() != 50)
        return 6;
    if (in_template<4>() != 5)
        return 3;
    {
        class Block : public Base {
            int point() const override
            {
                return 9;
            }
        };
        Block b;
        if (use(b) != 92)
            return 4;
    }
    return 0;
}
