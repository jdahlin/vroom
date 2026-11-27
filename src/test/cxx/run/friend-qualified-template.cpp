namespace ns {
template <typename T>
struct helper {
    int get(const T &x) const
    {
        return x.val;
    }
};
} // namespace ns

class Outer {
    struct Inner {
        int val;
    };

    friend struct ns::helper<Outer::Inner>;

    int secret;

public:
    Outer(int s) : secret(s) {}

    int run() const;
};

namespace ns {
template <>
struct helper<Outer::Inner> {
    int get(const Outer &o, const Outer::Inner &i) const
    {
        return o.secret + i.val;
    }
};
} // namespace ns

int Outer::run() const
{
    Inner in{17};
    struct ns::helper<Outer::Inner> h;
    return h.get(*this, in);
}

struct Plain {
    int val;
};

int main()
{
    Outer o(25);
    if (o.run() != 42)
        return 1;
    struct ns::helper<Plain> ph;
    Plain p{99};
    if (ph.get(p) != 99)
        return 2;
    return 0;
}
