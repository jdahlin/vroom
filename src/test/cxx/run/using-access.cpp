/* [namespace.udecl]/19: member using-declaration changing access. */

struct base {
  protected:
    int value;
    int get_double() const
    {
        return value * 2;
    }

  public:
    base(int v) : value(v) {}
    int pub = 10;
};

struct exposed : base {
    exposed(int v) : base(v) {}
    using base::get_double;
    using base::value;
};

class narrowed : public base {
    using base::pub;

  public:
    narrowed(int v) : base(v) {}
    int read_pub() const
    {
        return pub;
    }
};

template <typename Base> int extract_value(Base b)
{
    struct accessor : Base {
        accessor(Base x) : Base(x) {}
        using Base::value;
    };
    return accessor(b).value;
}

int main()
{
    exposed e(21);
    if (e.value != 21)
        return 1;
    if (e.get_double() != 42)
        return 2;
    e.value = 7;
    if (e.get_double() != 14)
        return 3;

    narrowed n(5);
    if (n.read_pub() != 10)
        return 4;

    base b(99);
    if (extract_value(b) != 99)
        return 5;

    return 0;
}
