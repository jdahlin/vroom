/* [dcl.constexpr]/10, [expr.const]/5: a constexpr object of class type
   initialized by a constexpr constructor whose arguments are empty-class
   prvalues (libc++'s std::nullopt) is constant-initialized
   ([basic.start.static]/2) */
struct nullopt_like {
    struct secret_tag {
        explicit secret_tag() = default;
    };
    constexpr explicit nullopt_like(secret_tag, secret_tag) {}
};
inline constexpr nullopt_like nullopt{nullopt_like::secret_tag{}, nullopt_like::secret_tag{}};

struct plain {};
struct holder {
    int a;
    constexpr holder(plain, int v) : a(v) {}
};
constexpr holder h{plain{}, 7};
constexpr holder h2(plain(), 8);
static_assert(h.a == 7 && h2.a == 8);

int main()
{
    volatile const holder *p = &h;
    volatile const holder *q = &h2;
    volatile const nullopt_like *n = &nullopt;
    if (p->a + q->a != 15)
        return 1;
    if (n == nullptr)
        return 2;
    return 0;
}
