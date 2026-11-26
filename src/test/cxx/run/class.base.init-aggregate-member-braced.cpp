/* [class.base.init]/7, /9 and [dcl.init.aggr]/4: a member of an aggregate
   class with a non-trivial element, initialized from a braced-init-list by a
   mem-initializer (({...}) and {...}) or a default member initializer: built
   in place, one copy per element (doctest's m_signature({name, file, line}))
 */
extern "C" int printf(const char *, ...);

struct S {
    int v;
    const S *self;
    S(int x) : v(x), self(this)
    {
        printf("make %d\n", v);
    }
    S(const S &o) : v(o.v), self(this)
    {
        printf("copy %d\n", v);
    }
    S(S &&o) : v(o.v), self(this)
    {
        printf("move %d\n", v);
    }
    ~S()
    {
        printf("gone %d %d\n", v, self == this);
    }
};

struct signature {
    S name;
    const char *file;
    int line;
};

struct two {
    signature first;
    S pair[2];
};

struct sub {
    signature paren;
    signature braced;
    signature defaulted{S(7), "d", 8};
    two nested;
    sub(const S &n, const char *f, int l)
        : paren({n, f, l}), braced{n, f, l + 1}, nested{{n, f, 9}, {n, 5}}
    {
    }
};

int main()
{
    S n(1);
    sub s(n, "g", 3);
    printf("%d %s %d %d %s\n", s.paren.name.v, s.paren.file, s.paren.line,
           s.braced.line, s.braced.file);
    printf("%d %d %s\n", s.defaulted.name.v, s.defaulted.line,
           s.defaulted.file);
    printf("%d %d %d %d\n", s.nested.first.name.v, s.nested.first.line,
           s.nested.pair[0].v, s.nested.pair[1].v);
    printf("%d %d %d\n", s.paren.name.self == &s.paren.name,
           s.defaulted.name.self == &s.defaulted.name,
           s.nested.pair[1].self == &s.nested.pair[1]);
}
