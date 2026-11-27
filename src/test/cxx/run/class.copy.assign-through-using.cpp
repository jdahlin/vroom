/* [class.copy.assign]/7, [namespace.udecl]/15: the shape of libc++'s
   variant: implicit assignments that call operator functions, through
   bases that bring operator= in by using-declarations, a union with a
   member whose assignment is not trivial, and a template operator= */
extern "C" int printf(const char *, ...);

static int moves;

struct Owner {
    int v = 0;
    Owner() = default;
    Owner(Owner &&o) : v(o.v)
    {
        o.v = 0;
    }
    Owner &operator=(Owner &&o)
    {
        moves++;
        v = o.v;
        o.v = 0;
        return *this;
    }
};

union Union {
    char dummy;
    Owner owner;
    Union() : owner() {}
    Union(const Union &) = default;
    Union(Union &&) = delete;
    Union &operator=(const Union &) = default;
    Union &operator=(Union &&) = default;
    ~Union() {}
};

struct Storage {
    Union data;
    int index = 0;
};

struct Destroying : Storage {
    using Storage::operator=;
    Destroying() = default;
    Destroying &operator=(const Destroying &) = default;
    Destroying &operator=(Destroying &&) = default;
};

struct Constructing : Destroying {
    using Destroying::operator=;
};

struct MoveAssigning : Constructing {
    using Constructing::operator=;
    MoveAssigning() = default;
    MoveAssigning &operator=(const MoveAssigning &) = default;
    MoveAssigning &operator=(MoveAssigning &&o)
    {
        data.owner = static_cast<Owner &&>(o.data.owner);
        index = o.index + 1;
        return *this;
    }
};

struct CopyAssigning : MoveAssigning {
    using MoveAssigning::operator=;
    CopyAssigning() = default;
    CopyAssigning &operator=(CopyAssigning &&) = default;
    CopyAssigning &operator=(const CopyAssigning &) = delete;
};

struct Impl : CopyAssigning {
    using CopyAssigning::operator=;
    Impl() = default;
    Impl &operator=(const Impl &) = default;
    Impl &operator=(Impl &&) = default;
};

struct Variant {
    Impl impl;
    Variant() = default;
    Variant &operator=(const Variant &) = default;
    Variant &operator=(Variant &&) = default;
    template <class T> Variant &operator=(T *value)
    {
        impl.data.owner.v = *value;
        return *this;
    }
};

struct Holder {
    Variant value;
    int tag = 0;
    Holder &operator=(Holder &&);
};
Holder &Holder::operator=(Holder &&) = default;

struct Base {
    int copies = 0, moved = 0;
    Base() = default;
    Base &operator=(const Base &o)
    {
        copies = o.copies + 1;
        return *this;
    }
    Base &operator=(Base &&o)
    {
        moved = o.moved + 1;
        return *this;
    }
};
struct Middle : Base {
    using Base::operator=;
};
struct Derived : Middle {
    using Middle::operator=;
    Derived &operator=(const Derived &) = default;
    Derived &operator=(Derived &&) = default;
};

/* the shape of libc++'s optional: a deleted defaulted move one beside a
   user copy one is ignored */
struct Payload {
    int v = 0;
    Payload &operator=(const Payload &o)
    {
        v = o.v + 1;
        return *this;
    }
};
struct Destruct {
    union {
        char null;
        Payload value;
    };
    Destruct() : value() {}
    ~Destruct() {}
};
struct CopyAssignBase : Destruct {
    CopyAssignBase() = default;
    CopyAssignBase &operator=(const CopyAssignBase &o)
    {
        value = o.value;
        return *this;
    }
    CopyAssignBase &operator=(CopyAssignBase &&) = default;
};
struct MoveAssignBase : CopyAssignBase {
    MoveAssignBase() = default;
    MoveAssignBase &operator=(const MoveAssignBase &) = default;
    MoveAssignBase &operator=(MoveAssignBase &&o)
    {
        value.v = o.value.v + 10;
        return *this;
    }
};
struct IteratorBase : MoveAssignBase {};
struct Optional : IteratorBase {
    Optional &operator=(const Optional &) = default;
    Optional &operator=(Optional &&) = default;
};

int main()
{
    Optional p, q;
    q.value.v = 5;
    p = q;
    int copied = p.value.v;
    p = static_cast<Optional &&>(q);
    printf("%d %d\n", copied, p.value.v);
    Holder a, b;
    int seven = 7;
    a.value = &seven;
    a.tag = 3;
    b = static_cast<Holder &&>(a);
    printf("%d %d %d %d %d\n", a.value.impl.data.owner.v,
           b.value.impl.data.owner.v, b.value.impl.index, b.tag, moves);
    Derived x, y;
    x = y;
    printf("%d %d\n", x.copies, x.moved);
    x = static_cast<Derived &&>(y);
    printf("%d %d\n", x.copies, x.moved);
    return 0;
}
