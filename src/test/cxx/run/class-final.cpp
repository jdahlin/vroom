// [class.pre]/1, [class.derived]/2, [meta.unary.prop]: final class-virt-specifier
// on classes with and without base clauses, and __is_final.
struct Base {
    int a;
};

struct Leaf final {
    int x;
};

class Derived final : public Base {
public:
    int b;
};

static_assert(__is_final(Leaf), "");
static_assert(__is_final(Derived), "");
static_assert(!__is_final(Base), "");
static_assert(!__is_final(int), "");

int main() {
    Leaf l{3};
    Derived d{{4}, 5};
    return l.x + d.a + d.b != 12;
}
