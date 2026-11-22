// Ordinary constructors do not participate in [class.prop]/1.
struct WithTemplate {
    int value;
    WithTemplate(const WithTemplate&) = default;
    WithTemplate& operator=(const WithTemplate&) = default;
    template<class T> WithTemplate(T v) : value(v) {}
};

struct WithoutDefault {
    WithoutDefault() = delete;
    int value;
};

struct Wrapper { WithTemplate member; };

static_assert(__is_trivially_copyable(WithTemplate));
static_assert(__is_trivially_copyable(Wrapper));
static_assert(__is_trivially_copyable(WithoutDefault));

int main() { return 0; }
